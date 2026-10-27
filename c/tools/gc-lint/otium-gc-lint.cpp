// otium-gc-lint: a static check for missing roots in the C runtime.
//
// The collector moves objects, and only values reachable from pushed oti_roots
// slots are updated. Growing one of ot_state's arrays (stack, frames,
// constants, ...) moves it too. The tool reports:
//
//   stale-value     an ot_value variable that isn't rooted, used after a call
//                   that can collect
//   stale-object    a pointer into a managed object (an oti_as_* cast,
//                   object_fields and so on) used after a call that can collect
//   stale-array     a pointer into rt->stack or another ot_state array used
//                   after a call that can grow that array
//   stale-argument  such a pointer, or the address of an unrooted value,
//                   passed to a parameter that the callee uses after the
//                   effect that invalidates it
//   unsequenced     an unrooted value passed next to an argument that can
//                   collect, which C may evaluate first
//
// The effects of each function come from the call graph of every file given,
// starting from the seeds in gc-lint.conf; a function that assigns rt->F or
// takes &rt->F (to pass to oti_grow, say) moves F. The first pass builds the
// call graph, the second checks each function with a flow analysis over its
// CFG.
//
// For debugging, OTIUM_GC_LINT_TRACE=function prints the flow state at each
// step of that function, and OTIUM_GC_LINT_TRACE_PARAMS=1 prints which
// parameters are used after which effects.

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ParentMapContext.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <vector>

using namespace clang;
using namespace clang::tooling;

namespace {

llvm::cl::OptionCategory category("otium-gc-lint options");
llvm::cl::list<std::string> config_paths("config",
                                         llvm::cl::desc("Seeds and exceptions (repeatable)"),
                                         llvm::cl::value_desc("file"), llvm::cl::cat(category));
llvm::cl::opt<bool> warnings_as_errors("warnings-as-errors",
                                       llvm::cl::desc("Exit with status 1 after any warning"),
                                       llvm::cl::init(false), llvm::cl::cat(category));
llvm::cl::list<std::string> why("why",
                                llvm::cl::desc("Explain the effects of a function, then exit"),
                                llvm::cl::value_desc("function"), llvm::cl::cat(category));
llvm::cl::opt<bool> list_effects("list-effects",
                                 llvm::cl::desc("List every function's effects, the functions "
                                                "that return pointers into objects and the "
                                                "indirect calls, then exit"),
                                 llvm::cl::init(false), llvm::cl::cat(category));

// Effects are tags: COLLECT, MOVE + field name, or MOVE_ANY.
const std::string COLLECT = "collect";
const std::string MOVE = "move:";
const std::string MOVE_ANY = "move:*";
// For a pointer parameter: what it points to may be a stale ot_value. A store
// through it, or a call that might store through it, makes it fresh again.
const std::string CONTENT = "content";
using Effects = std::set<std::string>;

bool has_effect(const Effects& effects, const std::string& tag) {
  return effects.count(tag) || (llvm::StringRef(tag).starts_with(MOVE) && effects.count(MOVE_ANY));
}

std::string describe(const std::string& tag) {
  if (tag == COLLECT || tag == CONTENT) return "can collect";
  if (tag == MOVE_ANY) return "can move any ot_state array";
  return "can move rt->" + tag.substr(MOVE.size());
}

struct Config {
  std::map<std::string, Effects> seeds;
  std::set<std::string> never, tag_only, derives, derive_prefixes, indirect_safe;
  // (function, variable) -> reason
  std::map<std::pair<std::string, std::string>, std::string> allow;
};

bool load_config(const std::string& path, Config& config) {
  std::ifstream in(path);
  if (!in) {
    llvm::errs() << "otium-gc-lint: cannot read " << path << "\n";
    return false;
  }
  std::string line;
  unsigned number = 0;
  while (std::getline(in, line)) {
    number++;
    if (size_t hash = line.find('#'); hash != std::string::npos) line.erase(hash);
    std::istringstream words(line);
    std::string key, name, extra;
    if (!(words >> key)) continue;
    if (!(words >> name)) {
      llvm::errs() << path << ":" << number << ": missing name\n";
      return false;
    }
    if (key == "collects") config.seeds[name].insert(COLLECT);
    else if (key == "grows-stack") config.seeds[name].insert(MOVE + "stack");
    else if (key == "moves" && (words >> extra)) config.seeds[name].insert(MOVE + extra);
    else if (key == "reenters") config.seeds[name].insert({COLLECT, MOVE_ANY});
    else if (key == "never") config.never.insert(name);
    else if (key == "tag-only") config.tag_only.insert(name);
    else if (key == "derives") config.derives.insert(name);
    else if (key == "derives-prefix") config.derive_prefixes.insert(name);
    else if (key == "indirect-safe") config.indirect_safe.insert(name);
    else if (key == "allow") {
      std::string variable, reason;
      words >> variable;
      std::getline(words, reason);
      size_t start = reason.find_first_not_of(" \t");
      if (variable.empty() || start == std::string::npos) {
        llvm::errs() << path << ":" << number << ": allow needs a function, variable and reason\n";
        return false;
      }
      config.allow[{name, variable}] = reason.substr(start);
    } else {
      llvm::errs() << path << ":" << number << ": cannot parse '" << key << "' entry\n";
      return false;
    }
  }
  return true;
}

// The whole-program call graph, by function key.
struct FunctionNode {
  std::string name;
  std::set<std::string> callees;
  bool defined = false;
  // Returns a pointer into a managed object, or the result of a call to one of
  // return_callees, which might.
  bool derives = false;
  std::set<std::string> return_callees;
  // Pointer parameters it might store through: stored through directly, or
  // passed on to another call.
  std::set<unsigned> out_params;
  Effects effects;
  // For each effect, the callee that brought it, or the reason.
  std::map<std::string, std::string> cause;
};

struct ParamKey {
  std::string function;
  unsigned index;

  bool operator<(const ParamKey& o) const {
    return function != o.function ? function < o.function : index < o.index;
  }
};

// A pointer that an effect invalidates, passed to a parameter that might be
// used after that effect. needs holds the sensitivity tags that make it a bug.
struct ArgumentFact {
  ParamKey param;
  std::set<std::string> needs;
  std::string location, message;
};

struct Diagnostic {
  std::string location, text;
  std::vector<std::pair<std::string, std::string>> notes;
};

struct Program {
  Config config;
  std::map<std::string, FunctionNode> functions;
  // Indirect calls that are assumed to do anything, by location.
  std::map<std::string, std::string> indirect_calls;
  // Phase 2. A parameter's sensitivity tags are the effects after which the
  // callee uses it; a tag ending in /w means it only stores through it.
  std::set<std::string> checked;
  std::map<ParamKey, std::set<std::string>> sensitive;
  std::map<ParamKey, std::string> sensitive_where;
  std::map<ParamKey, std::set<ParamKey>> forwards;
  std::vector<ArgumentFact> facts;
  std::vector<Diagnostic> diagnostics;
  std::set<std::string> diagnostic_keys;

  void report(Diagnostic diagnostic) {
    if (!diagnostic_keys.insert(diagnostic.location + diagnostic.text).second) return;
    diagnostics.push_back(std::move(diagnostic));
  }
};

std::string real_path(llvm::StringRef path) {
  llvm::SmallString<256> out;
  if (llvm::sys::fs::real_path(path, out)) return path.str();
  return std::string(out);
}

std::string location_string(const SourceManager& sources, SourceLocation location) {
  location = sources.getFileLoc(location);
  PresumedLoc presumed = sources.getPresumedLoc(location);
  if (presumed.isInvalid()) return "<unknown>";
  return real_path(presumed.getFilename()) + ":" + std::to_string(presumed.getLine()) + ":" +
         std::to_string(presumed.getColumn());
}

// Functions with internal linkage are qualified by their file.
std::string function_key(const FunctionDecl* function, const SourceManager& sources) {
  function = function->getCanonicalDecl();
  std::string name = function->getNameAsString();
  if (function->isExternallyVisible()) return name;
  SourceLocation location = sources.getFileLoc(function->getLocation());
  return real_path(sources.getFilename(location)) + "#" + name;
}

bool in_system(const SourceManager& sources, SourceLocation location) {
  return location.isInvalid() || sources.isInSystemHeader(sources.getFileLoc(location));
}

bool is_named_typedef(QualType type, llvm::StringRef name, const ASTContext& context) {
  for (unsigned i = 0; i < 16 && !type.isNull(); i++) {
    if (const auto* alias = dyn_cast<TypedefType>(type.getTypePtr()))
      if (alias->getDecl()->getName() == name) return true;
    QualType next = type.getSingleStepDesugaredType(context);
    if (next == type) break;
    type = next;
  }
  return false;
}

bool is_value_type(QualType type, const ASTContext& context) {
  return is_named_typedef(type, "ot_value", context);
}

bool is_value_array(QualType type, const ASTContext& context) {
  const ArrayType* array = context.getAsArrayType(type);
  return array != nullptr && is_value_type(array->getElementType(), context);
}

llvm::StringRef record_name(const RecordDecl* record) {
  if (!record->getName().empty()) return record->getName();
  if (const TypedefNameDecl* alias = record->getTypedefNameForAnonDecl()) return alias->getName();
  return "";
}

bool is_object_pointer(QualType type) {
  const auto* pointer = type->getAs<PointerType>();
  if (pointer == nullptr) return false;
  const auto* record = pointer->getPointeeType()->getAs<RecordType>();
  if (record == nullptr) return false;
  llvm::StringRef name = record_name(record->getDecl());
  return name == "oti_object_header" || (name.starts_with("oti_") && name.ends_with("_obj"));
}

bool is_data_pointer(QualType type) {
  const auto* pointer = type->getAs<PointerType>();
  return pointer != nullptr && !pointer->getPointeeType()->isFunctionType();
}

// rt->F for a pointer field F of ot_state, which growth moves.
std::optional<std::string> state_array(const Expr* e) {
  const auto* member = dyn_cast<MemberExpr>(e->IgnoreParens());
  if (member == nullptr || !member->isArrow()) return std::nullopt;
  const auto* field = dyn_cast<FieldDecl>(member->getMemberDecl());
  if (field == nullptr || record_name(field->getParent()) != "ot_state" ||
      !is_data_pointer(field->getType()))
    return std::nullopt;
  return field->getNameAsString();
}

// How gc-lint.conf names the target of an indirect call: function:variable
// for a local or parameter, Record.field for a struct field, or a global's name.
std::string indirect_name(const CallExpr* call) {
  const Expr* callee = call->getCallee()->IgnoreParenImpCasts();
  if (const auto* member = dyn_cast<MemberExpr>(callee)) {
    std::string field = member->getMemberDecl()->getNameAsString();
    if (const auto* decl = dyn_cast<FieldDecl>(member->getMemberDecl()))
      return record_name(decl->getParent()).str() + "." + field;
    return field;
  }
  if (const auto* reference = dyn_cast<DeclRefExpr>(callee)) {
    std::string name = reference->getDecl()->getNameAsString();
    if (const auto* owner =
            dyn_cast_or_null<FunctionDecl>(reference->getDecl()->getParentFunctionOrMethod()))
      return owner->getNameAsString() + ":" + name;
    return name;
  }
  return "";
}

bool indirect_safe(const Config& config, const CallExpr* call) {
  std::string name = indirect_name(call);
  return !name.empty() && config.indirect_safe.count(name) != 0;
}

bool configured_derive(const Config& config, llvm::StringRef name) {
  if (config.derives.count(name.str())) return true;
  for (const std::string& prefix : config.derive_prefixes)
    if (name.starts_with(prefix)) return true;
  return false;
}

const Stmt* parent_of(ASTContext& context, const Stmt* s) {
  DynTypedNodeList parents = context.getParents(*s);
  return parents.empty() ? nullptr : parents[0].get<Stmt>();
}

// Phase 1: the call graph.

class GraphVisitor : public RecursiveASTVisitor<GraphVisitor> {
public:
  GraphVisitor(Program& program, ASTContext& context)
      : program_(program), context_(context), sources_(context.getSourceManager()) {}

  bool TraverseFunctionDecl(FunctionDecl* function) {
    if (!function->doesThisDeclarationHaveABody() || in_system(sources_, function->getLocation()))
      return RecursiveASTVisitor::TraverseFunctionDecl(function);
    FunctionNode& node = program_.functions[function_key(function, sources_)];
    node.name = function->getNameAsString();
    node.defined = true;
    if (is_object_pointer(function->getReturnType()) ||
        configured_derive(program_.config, node.name))
      node.derives = true;
    FunctionNode* old = current_;
    current_ = &node;
    returns_.clear();
    stores_.clear();
    bool result = RecursiveASTVisitor::TraverseFunctionDecl(function);
    if (is_data_pointer(function->getReturnType())) {
      std::set<const VarDecl*> seen;
      for (const Expr* value : returns_) scan_return(value, seen);
    }
    current_ = old;
    return result;
  }

  bool VisitReturnStmt(ReturnStmt* statement) {
    if (current_ && statement->getRetValue()) returns_.push_back(statement->getRetValue());
    return true;
  }

  bool VisitVarDecl(VarDecl* variable) {
    if (current_ && variable->getInit()) stores_[variable].push_back(variable->getInit());
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator* binary) {
    if (current_ == nullptr || !binary->isAssignmentOp()) return true;
    if (const auto* reference = dyn_cast<DeclRefExpr>(binary->getLHS()->IgnoreParenImpCasts()))
      if (const auto* variable = dyn_cast<VarDecl>(reference->getDecl()))
        stores_[variable].push_back(binary->getRHS());
    if (std::optional<std::string> field = state_array(binary->getLHS())) moves(*field, binary);
    const Expr* target = binary->getLHS()->IgnoreParens();
    if (const auto* unary = dyn_cast<UnaryOperator>(target);
        unary && unary->getOpcode() == UO_Deref)
      out_param(unary->getSubExpr());
    else if (const auto* subscript = dyn_cast<ArraySubscriptExpr>(target))
      out_param(subscript->getBase());
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator* unary) {
    if (current_ && unary->getOpcode() == UO_AddrOf)
      if (std::optional<std::string> field = state_array(unary->getSubExpr())) moves(*field, unary);
    return true;
  }

  bool VisitCallExpr(CallExpr* call) {
    if (current_ == nullptr) return true;
    for (const Expr* argument : call->arguments()) out_param(argument);
    if (const FunctionDecl* callee = call->getDirectCallee()) {
      std::string key = function_key(callee, sources_);
      current_->callees.insert(key);
      FunctionNode& node = program_.functions[key];
      if (node.name.empty()) node.name = callee->getNameAsString();
    } else if (!indirect_safe(program_.config, call)) {
      std::string where = location_string(sources_, call->getExprLoc());
      for (const std::string& tag : {COLLECT, MOVE_ANY})
        if (current_->effects.insert(tag).second)
          current_->cause[tag] = "indirect call at " + where;
      std::string name = indirect_name(call);
      program_.indirect_calls[where] = name.empty() ? "an expression" : name;
    }
    return true;
  }

private:
  void out_param(const Expr* e) {
    if (const auto* reference = dyn_cast<DeclRefExpr>(e->IgnoreParenCasts()))
      if (const auto* param = dyn_cast<ParmVarDecl>(reference->getDecl()))
        if (is_data_pointer(param->getType()))
          current_->out_params.insert(param->getFunctionScopeIndex());
  }

  void moves(const std::string& field, const Stmt* where) {
    // Reading the field's address only to store through it doesn't move it,
    // but nothing here tells the two apart, so any &rt->F counts.
    std::string tag = MOVE + field;
    if (current_->effects.insert(tag).second)
      current_->cause[tag] =
          "changes rt->" + field + " at " + location_string(sources_, where->getBeginLoc());
  }

  // Whether a returned pointer comes from a managed object.
  void scan_return(const Stmt* s, std::set<const VarDecl*>& seen) {
    if (s == nullptr) return;
    if (const auto* reference = dyn_cast<DeclRefExpr>(s))
      if (const auto* variable = dyn_cast<VarDecl>(reference->getDecl()))
        if (variable->isLocalVarDecl() && seen.insert(variable).second)
          for (const Expr* value : stores_[variable]) scan_return(value, seen);
    if (const auto* call = dyn_cast<CallExpr>(s)) {
      if (const FunctionDecl* callee = call->getDirectCallee())
        current_->return_callees.insert(function_key(callee, sources_));
      return;
    }
    if (const auto* cast = dyn_cast<CastExpr>(s))
      if (cast->getCastKind() == CK_IntegralToPointer) {
        const Expr* inner = cast->getSubExpr()->IgnoreParens();
        if (is_value_type(inner->getType(), context_) && !inner->isIntegerConstantExpr(context_))
          current_->derives = true;
      }
    for (const Stmt* child : s->children()) scan_return(child, seen);
  }

  Program& program_;
  ASTContext& context_;
  const SourceManager& sources_;
  FunctionNode* current_ = nullptr;
  std::vector<const Expr*> returns_;
  std::map<const VarDecl*, std::vector<const Expr*>> stores_;
};

void finish_graph(Program& program) {
  for (auto& [key, node] : program.functions) {
    if (program.config.never.count(node.name)) {
      node.effects.clear();
      continue;
    }
    auto seed = program.config.seeds.find(node.name);
    if (seed != program.config.seeds.end())
      for (const std::string& tag : seed->second)
        if (node.effects.insert(tag).second || node.cause[tag].empty())
          node.cause[tag] = "listed in gc-lint.conf";
  }
  bool changed;
  do {
    changed = false;
    for (auto& [key, node] : program.functions)
      if (!node.derives)
        for (const std::string& callee : node.return_callees)
          if (program.functions[callee].derives) {
            node.derives = true;
            changed = true;
            break;
          }
  } while (changed);
  do {
    changed = false;
    for (auto& [key, node] : program.functions) {
      if (program.config.never.count(node.name)) continue;
      for (const std::string& callee_key : node.callees) {
        const FunctionNode& callee = program.functions[callee_key];
        for (const std::string& tag : callee.effects)
          if (node.effects.insert(tag).second) {
            node.cause[tag] = callee_key;
            changed = true;
          }
      }
    }
  } while (changed);
}

// f -> g -> ... -> the function or reason that brings the effect.
std::string chain(const Program& program, const std::string& key, const std::string& tag) {
  std::string out, current = key;
  std::set<std::string> seen;
  while (seen.insert(current).second) {
    auto found = program.functions.find(current);
    if (found == program.functions.end()) break;
    if (!out.empty()) out += " -> ";
    out += found->second.name;
    auto cause = found->second.cause.find(tag);
    if (cause == found->second.cause.end() && llvm::StringRef(tag).starts_with(MOVE))
      cause = found->second.cause.find(MOVE_ANY);
    if (cause == found->second.cause.end()) break;
    if (program.functions.count(cause->second) == 0) {
      out += " (" + cause->second + ")";
      break;
    }
    current = cause->second;
  }
  return out;
}

// Phase 2: checking each function.

enum VariableKind { VALUE, VALUE_ARRAY, POINTER };

struct Variable {
  VariableKind kind;
  int param = -1;
};

// What a variable holds, or where a pointer points.
enum : unsigned {
  LIVE = 1,      // an ot_value that may be a managed pointer
  HEAP = 2,      // points into a managed object
  UNROOTED = 4,  // the address of an unrooted ot_value variable
};

struct Provenance {
  unsigned bits = 0;
  std::set<std::string> arrays;  // ot_state arrays it points into
  std::set<int> params;          // pointer parameters it was derived from

  void merge(const Provenance& o) {
    bits |= o.bits;
    arrays.insert(o.arrays.begin(), o.arrays.end());
    params.insert(o.params.begin(), o.params.end());
  }

  bool operator==(const Provenance& o) const {
    return bits == o.bits && arrays == o.arrays && params == o.params;
  }
};

// The call after which a variable may be stale, the effect, and for a value
// derived from a parameter, which one.
struct Origin {
  const CallExpr* call;
  std::string tag;
  int param;

  bool operator<(const Origin& o) const {
    return std::tie(call, tag, param) < std::tie(o.call, o.tag, o.param);
  }

  bool operator==(const Origin& o) const {
    return call == o.call && tag == o.tag && param == o.param;
  }
};

struct VariableState {
  Provenance provenance;
  std::set<Origin> stale;

  bool operator==(const VariableState& o) const {
    return provenance == o.provenance && stale == o.stale;
  }
};

struct FlowState {
  bool reachable = false;
  std::map<const VarDecl*, VariableState> variables;
  // Rooted on every path here.
  std::set<const VarDecl*> rooted;

  bool operator==(const FlowState& o) const {
    return reachable == o.reachable && variables == o.variables && rooted == o.rooted;
  }
};

void merge_into(FlowState& into, const FlowState& from) {
  if (!from.reachable) return;
  if (!into.reachable) {
    into = from;
    return;
  }
  for (const auto& [variable, state] : from.variables) {
    VariableState& target = into.variables[variable];
    target.provenance.merge(state.provenance);
    target.stale.insert(state.stale.begin(), state.stale.end());
  }
  std::set<const VarDecl*> both;
  for (const VarDecl* variable : into.rooted)
    if (from.rooted.count(variable)) both.insert(variable);
  into.rooted = std::move(both);
}

class FunctionChecker {
public:
  FunctionChecker(Program& program, ASTContext& context, const FunctionDecl* function)
      : program_(program), context_(context), sources_(context.getSourceManager()),
        function_(function), key_(function_key(function, sources_)) {}

  void run() {
    if (const char* name = getenv("OTIUM_GC_LINT_TRACE")) tracing_ = function_->getName() == name;
    collect_variables();
    CFG::BuildOptions options;
    options.setAllAlwaysAdd();
    std::unique_ptr<CFG> cfg = CFG::buildCFG(function_, function_->getBody(), &context_, options);
    if (cfg == nullptr) return;
    collect_root_pushes(*cfg);

    unsigned blocks = cfg->getNumBlockIDs();
    std::vector<FlowState> in(blocks), out(blocks), out_true(blocks);
    std::vector<const BinaryOperator*> conjunction(blocks);
    for (const CFGBlock* block : *cfg)
      conjunction[block->getBlockID()] = branch_conjunction(*block);
    FlowState entry;
    entry.reachable = true;
    for (const auto& [variable, info] : variables_) {
      if (info.param < 0) continue;
      Provenance& p = entry.variables[variable].provenance;
      if (info.kind == VALUE) p.bits = LIVE;
      else if (is_object_pointer(variable->getType())) p.bits = HEAP;
      else p.params.insert(info.param);
    }
    std::vector<const CFGBlock*> order = reverse_post_order(*cfg);
    bool changed;
    unsigned rounds = 0;
    do {
      changed = false;
      for (const CFGBlock* block : order) {
        unsigned id = block->getBlockID();
        FlowState merged;
        if (block == &cfg->getEntry()) merged = entry;
        for (const CFGBlock::AdjacentBlock& predecessor : block->preds())
          if (const CFGBlock* p = predecessor.getReachableBlock()) {
            unsigned pid = p->getBlockID();
            bool true_edge = conjunction[pid] && p->succ_size() == 2 &&
                             p->succs().begin()->getReachableBlock() == block;
            merge_into(merged, true_edge ? out_true[pid] : out[pid]);
          }
        FlowState next = merged;
        transfer(*block, next, false);
        if (conjunction[id]) {
          // The branch is taken only when every operand was true, so leave out
          // the paths that stopped early at a false operand.
          std::set<const Stmt*> early;
          spine(conjunction[id], early);
          FlowState taken;
          for (const CFGBlock::AdjacentBlock& predecessor : block->preds())
            if (const CFGBlock* p = predecessor.getReachableBlock())
              if (!early.count(p->getTerminatorStmt())) merge_into(taken, out[p->getBlockID()]);
          transfer(*block, taken, false);
          if (!(taken == out_true[id])) changed = true;
          out_true[id] = std::move(taken);
        }
        if (!(next == out[id]) || !(merged == in[id])) changed = true;
        in[id] = std::move(merged);
        out[id] = std::move(next);
      }
    } while (changed && ++rounds < 500);
    if (changed) {
      llvm::errs() << "otium-gc-lint: gave up on " << function_->getName()
                   << ": the analysis did not settle\n";
      return;
    }

    for (const CFGBlock* block : order) {
      FlowState state = in[block->getBlockID()];
      if (state.reachable) transfer(*block, state, true);
    }
  }

private:
  // The && operators whose false operands branch straight to the end of e.
  static void spine(const Expr* e, std::set<const Stmt*>& out) {
    const auto* binary = dyn_cast<BinaryOperator>(e->IgnoreParens());
    if (binary == nullptr || binary->getOpcode() != BO_LAnd) return;
    out.insert(binary);
    spine(binary->getLHS(), out);
    spine(binary->getRHS(), out);
  }

  // A branch on a && b that the CFG doesn't split because the operator sits
  // inside another expression, as in if (__builtin_expect(!!(a && b), 1)).
  static const BinaryOperator* branch_conjunction(const CFGBlock& block) {
    const Stmt* terminator = block.getTerminatorStmt();
    const Expr* condition = nullptr;
    if (const auto* s = dyn_cast_or_null<IfStmt>(terminator)) condition = s->getCond();
    else if (const auto* s = dyn_cast_or_null<WhileStmt>(terminator)) condition = s->getCond();
    if (condition == nullptr || block.succ_size() != 2) return nullptr;
    while (true) {
      condition = condition->IgnoreParenImpCasts();
      if (const auto* call = dyn_cast<CallExpr>(condition)) {
        if (call->getBuiltinCallee() != Builtin::BI__builtin_expect || call->getNumArgs() != 2)
          return nullptr;
        condition = call->getArg(0);
        continue;
      }
      if (const auto* unary = dyn_cast<UnaryOperator>(condition)) {
        const auto* inner = dyn_cast<UnaryOperator>(unary->getSubExpr()->IgnoreParenImpCasts());
        if (unary->getOpcode() != UO_LNot || !inner || inner->getOpcode() != UO_LNot)
          return nullptr;
        condition = inner->getSubExpr();
        continue;
      }
      break;
    }
    const auto* binary = dyn_cast<BinaryOperator>(condition);
    if (binary == nullptr || binary->getOpcode() != BO_LAnd) return nullptr;
    for (const CFGElement& element : block)
      if (std::optional<CFGStmt> statement = element.getAs<CFGStmt>())
        if (statement->getStmt() == binary) return binary;
    return nullptr;
  }

  static std::vector<const CFGBlock*> reverse_post_order(const CFG& cfg) {
    std::vector<const CFGBlock*> order;
    std::set<const CFGBlock*> seen{&cfg.getEntry()};
    std::vector<std::pair<const CFGBlock*, unsigned>> stack{{&cfg.getEntry(), 0}};
    while (!stack.empty()) {
      auto& [block, next] = stack.back();
      if (next < block->succ_size()) {
        const CFGBlock* successor = block->succs().begin()[next++].getReachableBlock();
        if (successor && seen.insert(successor).second) stack.push_back({successor, 0});
        continue;
      }
      order.push_back(block);
      stack.pop_back();
    }
    std::reverse(order.begin(), order.end());
    return order;
  }

  void collect_variables() {
    for (unsigned i = 0; i < function_->getNumParams(); i++) {
      const ParmVarDecl* param = function_->getParamDecl(i);
      QualType type = param->getType();
      if (is_value_type(type, context_)) variables_[param] = {VALUE, (int)i};
      else if (is_data_pointer(type) && !is_runtime_pointer(type))
        variables_[param] = {POINTER, (int)i};
    }

    struct Locals : RecursiveASTVisitor<Locals> {
      FunctionChecker& self;

      explicit Locals(FunctionChecker& self) : self(self) {}

      bool VisitVarDecl(VarDecl* variable) {
        if (!variable->isLocalVarDecl() || variable->isStaticLocal()) return true;
        QualType type = variable->getType();
        if (is_value_type(type, self.context_)) self.variables_[variable] = {VALUE};
        else if (is_value_array(type, self.context_)) self.variables_[variable] = {VALUE_ARRAY};
        else if (is_data_pointer(type)) self.variables_[variable] = {POINTER};
        return true;
      }
    } locals(*this);

    locals.TraverseStmt(function_->getBody());
  }

  // The runtime and heap themselves never move.
  static bool is_runtime_pointer(QualType type) {
    const auto* record = type->getPointeeType()->getAs<RecordType>();
    if (record == nullptr) return false;
    llvm::StringRef name = record_name(record->getDecl());
    return name == "ot_state" || name == "oti_heap";
  }

  // Which variables each oti_roots record holds, from every push in the body.
  void collect_root_pushes(const CFG& cfg) {
    for (const CFGBlock* block : cfg)
      for (const CFGElement& element : *block)
        if (std::optional<CFGStmt> statement = element.getAs<CFGStmt>())
          if (const auto* call = dyn_cast<CallExpr>(statement->getStmt()))
            if (callee_name(call) == "oti_roots_push" && call->getNumArgs() == 4) {
              const VarDecl* record = address_of_variable(call->getArg(1));
              const VarDecl* slots = rooted_slots(call->getArg(2));
              if (record && slots) roots_[record].insert(slots);
            }
  }

  static llvm::StringRef callee_name(const CallExpr* call) {
    const FunctionDecl* callee = call->getDirectCallee();
    return callee == nullptr || callee->getIdentifier() == nullptr ? "" : callee->getName();
  }

  static const Expr* strip(const Expr* e) {
    while (true) {
      e = e->IgnoreParens();
      const auto* c = dyn_cast<CastExpr>(e);
      if (c == nullptr) return e;
      e = c->getSubExpr();
    }
  }

  const VarDecl* variable_of(const Expr* e) const {
    if (const auto* reference = dyn_cast<DeclRefExpr>(strip(e)))
      if (const auto* variable = dyn_cast<VarDecl>(reference->getDecl())) return variable;
    return nullptr;
  }

  // &variable
  const VarDecl* address_of_variable(const Expr* e) const {
    if (const auto* unary = dyn_cast<UnaryOperator>(strip(e)))
      if (unary->getOpcode() == UO_AddrOf) return variable_of(unary->getSubExpr());
    return nullptr;
  }

  // What an oti_roots_push slots argument roots: &value, an array, or what a
  // pointer variable points to.
  const VarDecl* rooted_slots(const Expr* e) const {
    const VarDecl* v = address_of_variable(e);
    if (v == nullptr) v = variable_of(e);
    return v && variables_.count(v) ? v : nullptr;
  }

  Effects call_effects(const CallExpr* call) const {
    const FunctionDecl* callee = call->getDirectCallee();
    if (callee == nullptr)
      return indirect_safe(program_.config, call) ? Effects{} : Effects{COLLECT, MOVE_ANY};
    auto found = program_.functions.find(function_key(callee, sources_));
    return found == program_.functions.end() ? Effects{} : found->second.effects;
  }

  bool derives(const CallExpr* call) const {
    const FunctionDecl* callee = call->getDirectCallee();
    if (callee == nullptr || callee->getIdentifier() == nullptr) return false;
    if (configured_derive(program_.config, callee->getName())) return true;
    auto found = program_.functions.find(function_key(callee, sources_));
    return found != program_.functions.end() && found->second.derives;
  }

  // Where the memory designated by an lvalue lives.
  Provenance lvalue_provenance(const Expr* e, const FlowState& state) const {
    e = e->IgnoreParens();
    if (const auto* member = dyn_cast<MemberExpr>(e))
      return member->isArrow() ? provenance(member->getBase(), state)
                               : lvalue_provenance(member->getBase(), state);
    if (const auto* subscript = dyn_cast<ArraySubscriptExpr>(e))
      return provenance(subscript->getBase(), state);
    if (const auto* unary = dyn_cast<UnaryOperator>(e))
      if (unary->getOpcode() == UO_Deref) return provenance(unary->getSubExpr(), state);
    if (const VarDecl* variable = variable_of(e)) {
      auto found = variables_.find(variable);
      if (found != variables_.end() && found->second.kind != POINTER &&
          !state.rooted.count(variable)) {
        Provenance p;
        p.bits = UNROOTED;
        return p;
      }
    }
    return {};
  }

  // Where a pointer-valued expression points.
  Provenance provenance(const Expr* e, const FlowState& state) const {
    e = e->IgnoreParens();
    if (std::optional<std::string> field = state_array(e)) {
      Provenance p;
      p.arrays.insert(*field);
      return p;
    }
    if (const auto* cast = dyn_cast<CastExpr>(e)) {
      if (cast->getCastKind() == CK_ArrayToPointerDecay)
        return lvalue_provenance(cast->getSubExpr(), state);
      if (cast->getCastKind() == CK_IntegralToPointer) {
        const Expr* inner = cast->getSubExpr()->IgnoreParens();
        Provenance p;
        if (is_value_type(inner->getType(), context_) && !inner->isIntegerConstantExpr(context_))
          p.bits = HEAP;
        return p;
      }
      return provenance(cast->getSubExpr(), state);
    }
    if (const auto* reference = dyn_cast<DeclRefExpr>(e)) {
      const auto* variable = dyn_cast<VarDecl>(reference->getDecl());
      auto found = variable ? state.variables.find(variable) : state.variables.end();
      if (found == state.variables.end() || variables_.at(variable).kind != POINTER) return {};
      Provenance p = found->second.provenance;
      p.bits &= HEAP;
      return p;
    }
    if (const auto* unary = dyn_cast<UnaryOperator>(e)) {
      if (unary->getOpcode() == UO_AddrOf) return lvalue_provenance(unary->getSubExpr(), state);
      if (unary->isIncrementDecrementOp()) return provenance(unary->getSubExpr(), state);
      return {};
    }
    if (const auto* binary = dyn_cast<BinaryOperator>(e)) {
      if (binary->isAdditiveOp() || binary->isCompoundAssignmentOp()) {
        Provenance p;
        if (binary->getLHS()->getType()->isPointerType())
          p.merge(provenance(binary->getLHS(), state));
        if (binary->getRHS()->getType()->isPointerType() && binary->isAdditiveOp())
          p.merge(provenance(binary->getRHS(), state));
        return p;
      }
      if (binary->getOpcode() == BO_Comma || binary->getOpcode() == BO_Assign)
        return provenance(binary->getRHS(), state);
      return {};
    }
    if (const auto* conditional = dyn_cast<AbstractConditionalOperator>(e)) {
      Provenance p = provenance(conditional->getTrueExpr(), state);
      p.merge(provenance(conditional->getFalseExpr(), state));
      return p;
    }
    if (const auto* call = dyn_cast<CallExpr>(e)) {
      if (derives(call)) return {HEAP, {}, {}};
      // Library functions that return a pointer into their first argument.
      static const std::set<std::string> searches = {"memchr",  "memrchr", "memmem", "strchr",
                                                     "strrchr", "strstr",  "strpbrk"};
      if (call->getNumArgs() > 0 && searches.count(callee_name(call).str())) {
        Provenance p = provenance(call->getArg(0), state);
        p.bits &= HEAP;
        return p;
      }
    }
    return {};
  }

  // Whether an ot_value expression certainly holds an immediate.
  bool immediate(const Expr* e) const {
    e = e->IgnoreParenImpCasts();
    if (e->isValueDependent()) return false;
    if (e->isIntegerConstantExpr(context_)) return true;
    if (const auto* call = dyn_cast<CallExpr>(e)) {
      llvm::StringRef name = callee_name(call);
      return name == "ot_int" || name == "ot_bool";
    }
    if (const auto* conditional = dyn_cast<ConditionalOperator>(e))
      return immediate(conditional->getTrueExpr()) && immediate(conditional->getFalseExpr());
    if (const auto* list = dyn_cast<InitListExpr>(e)) {
      for (const Expr* init : list->inits())
        if (!immediate(init)) return false;
      return true;
    }
    return isa<ImplicitValueInitExpr>(e);
  }

  const Stmt* parent(const Stmt* s) const { return parent_of(context_, s); }

  // The nearest parent other than parentheses and implicit casts. truth is
  // set when one of the casts tests the value for truth.
  const Stmt* user(const Stmt* s, const Stmt** child, bool* truth = nullptr) const {
    const Stmt* p = parent(s);
    while (p && (isa<ParenExpr>(p) || isa<ImplicitCastExpr>(p))) {
      if (const auto* cast = dyn_cast<ImplicitCastExpr>(p))
        if (truth && cast->getCastKind() == CK_IntegralToBoolean) *truth = true;
      s = p;
      p = parent(p);
    }
    *child = s;
    return p;
  }

  // Whether a reference reads the variable in a way that a move can change.
  // Tag tests, comparisons with constants and truth tests can't tell a stale
  // copy from a fresh one.
  bool is_use(const DeclRefExpr* reference, const Variable& info) const {
    const Stmt* child;
    bool truth = false;
    const Stmt* p = user(reference, &child, &truth);
    if (truth && info.kind == VALUE) return false;
    for (const Stmt* a = p; a; a = parent(a))
      if (isa<UnaryExprOrTypeTraitExpr>(a)) return false;
    if (p == nullptr) return true;
    if (const auto* binary = dyn_cast<BinaryOperator>(p)) {
      if (binary->getOpcode() == BO_Assign && binary->getLHS()->IgnoreParens() == reference)
        return false;
      if (info.kind == VALUE) {
        if (binary->isEqualityOp()) {
          const Expr* other = binary->getLHS() == child ? binary->getRHS() : binary->getLHS();
          if (!other->isValueDependent() && other->isIntegerConstantExpr(context_)) return false;
        }
        if (binary->isBitwiseOp() || binary->isShiftOp()) return false;
      }
    }
    if (const auto* unary = dyn_cast<UnaryOperator>(p)) {
      // Addresses are handled where they are passed.
      if (unary->getOpcode() == UO_AddrOf) return false;
      if (info.kind == VALUE && unary->getOpcode() == UO_LNot) return false;
    }
    if (info.kind == VALUE)
      if (const auto* call = dyn_cast<CallExpr>(p))
        if (call->getCallee() != child && program_.config.tag_only.count(callee_name(call).str()))
          return false;
    if (info.kind == VALUE_ARRAY && stored(p)) return false;
    return true;
  }

  // Whether e (*p or p[i]) is the target of an assignment.
  bool stored(const Stmt* e) const {
    const Stmt* child;
    const Stmt* assignment = user(e, &child);
    const auto* binary = dyn_cast_or_null<BinaryOperator>(assignment);
    return binary && binary->getOpcode() == BO_Assign && binary->getLHS() == child;
  }

  // Whether a reference is an argument to a function defined in the files checked.
  bool passed_on(const DeclRefExpr* reference) const {
    const Stmt* child;
    const auto* call = dyn_cast_or_null<CallExpr>(user(reference, &child));
    if (call == nullptr || call->getCallee() == child || !call->getDirectCallee()) return false;
    auto found = program_.functions.find(function_key(call->getDirectCallee(), sources_));
    return found != program_.functions.end() && found->second.defined;
  }

  // p in *p = x or p[i] = x.
  bool store_only(const DeclRefExpr* reference) const {
    const Stmt* child;
    const Stmt* p = user(reference, &child);
    if (const auto* unary = dyn_cast_or_null<UnaryOperator>(p))
      return unary->getOpcode() == UO_Deref && stored(unary);
    if (const auto* subscript = dyn_cast_or_null<ArraySubscriptExpr>(p))
      return subscript->getBase() == child && stored(subscript);
    return false;
  }

  bool allowed(const VarDecl* variable) const {
    return program_.config.allow.count(
               {function_->getNameAsString(), variable->getNameAsString()}) != 0;
  }

  void add_effect_note(Diagnostic& d, const Origin& origin) const {
    std::string where = location_string(sources_, origin.call->getExprLoc());
    if (const FunctionDecl* callee = origin.call->getDirectCallee())
      d.notes.push_back({where, "'" + callee->getNameAsString() + "' " + describe(origin.tag) +
                                    ": " +
                                    chain(program_, function_key(callee, sources_), origin.tag)});
    else d.notes.push_back({where, "an indirect call " + describe(origin.tag)});
  }

  void use(const DeclRefExpr* reference, const VarDecl* variable, FlowState& state, bool report) {
    if (state.rooted.count(variable)) return;
    auto found = state.variables.find(variable);
    if (found == state.variables.end() || found->second.stale.empty()) return;
    const Variable& info = variables_.at(variable);
    bool store = store_only(reference);
    bool forwarded = passed_on(reference);
    for (const Origin& origin : found->second.stale) {
      if (origin.tag == CONTENT && store) continue;
      if (origin.param >= 0) {
        // The callee's own uses decide, through the forwarding edge.
        if (forwarded) continue;
        // Stores through the pointer count separately: an out parameter for
        // an unrooted variable can be written after a collection, not read.
        std::string tag = store ? origin.tag + "/w" : origin.tag;
        ParamKey key{key_, (unsigned)origin.param};
        if (tracing_)
          llvm::errs() << "  sensitive " << origin.param << " " << tag << " at "
                       << location_string(sources_, reference->getLocation()) << "\n";
        if (program_.sensitive[key].insert(tag).second && !program_.sensitive_where.count(key))
          program_.sensitive_where[key] = location_string(sources_, reference->getLocation()) +
                                          ", after the call at " +
                                          location_string(sources_, origin.call->getExprLoc());
        continue;
      }
      if (!report || allowed(variable) || !reported_.insert({variable, origin}).second) continue;
      Diagnostic d;
      d.location = location_string(sources_, reference->getLocation());
      std::string name = "'" + variable->getNameAsString() + "'";
      if (info.kind != POINTER)
        d.text = name + " is not rooted and may be stale after a call that can collect "
                        "[otium-gc-stale-value]";
      else if (origin.tag == COLLECT)
        d.text = name + " points into a managed object and may be stale after a call that "
                        "can collect [otium-gc-stale-object]";
      else {
        std::string array = origin.tag.substr(MOVE.size());
        d.text = name + " points into rt->" + array +
                 " and may be stale after a call that can move it [otium-gc-stale-array]";
      }
      add_effect_note(d, origin);
      program_.report(std::move(d));
    }
    if (store) refresh_content(found->second);
  }

  static void refresh_content(VariableState& s) {
    for (auto i = s.stale.begin(); i != s.stale.end();)
      i = i->tag == CONTENT ? s.stale.erase(i) : std::next(i);
  }

  void reset(const VarDecl* variable, Provenance p, FlowState& state) {
    VariableState& s = state.variables[variable];
    s.provenance = std::move(p);
    s.stale.clear();
  }

  Provenance assigned(const VarDecl* variable, const Expr* value, const FlowState& state) const {
    if (variables_.at(variable).kind == POINTER) return provenance(value, state);
    Provenance p;
    if (!immediate(value)) p.bits = LIVE;
    return p;
  }

  bool contains_collecting_call(const Stmt* s) const {
    if (s == nullptr) return false;
    if (const auto* call = dyn_cast<CallExpr>(s))
      if (has_effect(call_effects(call), COLLECT)) return true;
    for (const Stmt* child : s->children())
      if (contains_collecting_call(child)) return true;
    return false;
  }

  void check_unsequenced(const CallExpr* call, const FlowState& state) {
    std::vector<bool> collects;
    unsigned count = 0;
    for (const Expr* argument : call->arguments()) {
      collects.push_back(contains_collecting_call(argument));
      count += collects.back();
    }
    if (count == 0) return;
    for (unsigned i = 0; i < call->getNumArgs(); i++) {
      if (count == 1 && collects[i]) continue;
      const Expr* argument = call->getArg(i);
      const VarDecl* variable = variable_of(argument);
      std::string what;
      if (variable && variables_.count(variable)) {
        if (state.rooted.count(variable) || allowed(variable)) continue;
        auto found = state.variables.find(variable);
        unsigned bits = found == state.variables.end() ? 0 : found->second.provenance.bits;
        if (variables_.at(variable).kind == VALUE && (bits & LIVE)) what = "value";
        else if (variables_.at(variable).kind == POINTER && (bits & HEAP)) what = "pointer";
      } else if (!variable && (provenance(argument, state).bits & HEAP)) {
        what = "pointer";
      }
      if (what.empty()) continue;
      Diagnostic d;
      d.location = location_string(sources_, argument->getExprLoc());
      d.text = "this " + what +
               " argument may be evaluated before another argument that can collect "
               "[otium-gc-unsequenced]";
      for (unsigned j = 0; j < call->getNumArgs(); j++)
        if (j != i && collects[j])
          d.notes.push_back({location_string(sources_, call->getArg(j)->getExprLoc()),
                             "this argument can collect"});
      program_.report(std::move(d));
    }
  }

  // Whether a call might store through argument j. A callee defined in the
  // files checked stores through only the parameters it writes or passes on.
  bool may_store(const FunctionDecl* callee, unsigned j) const {
    if (callee == nullptr || j >= callee->getNumParams()) return true;
    if (const auto* pointer = callee->getParamDecl(j)->getType()->getAs<PointerType>())
      if (pointer->getPointeeType().isConstQualified()) return false;
    auto found = program_.functions.find(function_key(callee, sources_));
    if (found == program_.functions.end() || !found->second.defined) return true;
    return found->second.out_params.count(j) != 0;
  }

  // Record pointer arguments that the callee might use after it invalidates them.
  void check_arguments(const CallExpr* call, const FlowState& state, bool report,
                       std::vector<const VarDecl*>& written,
                       std::vector<const VarDecl*>& refreshed) {
    const FunctionDecl* callee = call->getDirectCallee();
    for (unsigned j = 0; j < call->getNumArgs(); j++) {
      const Expr* argument = call->getArg(j);
      if (!argument->getType()->isPointerType()) continue;
      const VarDecl* address = address_of_variable(argument);
      if (!address)
        if (const VarDecl* v = variable_of(argument);
            v && variables_.count(v) && variables_.at(v).kind == VALUE_ARRAY)
          address = v;
      if (address && !variables_.count(address)) address = nullptr;
      if (address && may_store(callee, j)) written.push_back(address);
      if (const VarDecl* v = variable_of(argument);
          v && variables_.count(v) && variables_.at(v).kind == POINTER && may_store(callee, j))
        refreshed.push_back(v);
      if (!callee || j >= callee->getNumParams()) continue;

      Provenance p = provenance(argument, state);
      ParamKey key{function_key(callee, sources_), j};
      for (int param : p.params) program_.forwards[{key_, (unsigned)param}].insert(key);
      if (!report) continue;
      if (p.bits & UNROOTED) {
        auto found = state.variables.find(address);
        if (!address || found == state.variables.end() || !(found->second.provenance.bits & LIVE) ||
            allowed(address))
          p.bits &= ~UNROOTED;
      }
      if (const VarDecl* v = variable_of(argument); v && allowed(v)) continue;
      // Reads matter for all of these; stores only for memory that moves.
      std::set<std::string> needs;
      std::string message;
      if (p.bits & UNROOTED) {
        needs.insert(CONTENT);
        message = "the address of unrooted '" + address->getNameAsString() + "'";
      }
      if (p.bits & HEAP) {
        needs.insert({COLLECT, COLLECT + "/w"});
        message = "a pointer into a managed object";
      }
      for (const std::string& array : p.arrays) {
        needs.insert({MOVE + array, MOVE + array + "/w"});
        message = "a pointer into rt->" + array;
      }
      if (!needs.empty())
        program_.facts.push_back(
            {key, needs, location_string(sources_, argument->getExprLoc()), message});
    }
  }

  void call(const CallExpr* call, FlowState& state, bool report) {
    llvm::StringRef name = callee_name(call);
    if (name == "oti_roots_push" && call->getNumArgs() == 4) {
      if (const VarDecl* slots = rooted_slots(call->getArg(2))) state.rooted.insert(slots);
      return;
    }
    if (name == "oti_roots_pop" && call->getNumArgs() == 2) {
      if (const VarDecl* record = address_of_variable(call->getArg(1))) {
        auto found = roots_.find(record);
        if (found != roots_.end())
          for (const VarDecl* variable : found->second) state.rooted.erase(variable);
      }
      return;
    }
    if (report) check_unsequenced(call, state);
    std::vector<const VarDecl*> written, refreshed;
    check_arguments(call, state, report, written, refreshed);

    Effects effects = call_effects(call);
    if (!effects.empty()) {
      bool collects = has_effect(effects, COLLECT);
      for (auto& [variable, s] : state.variables) {
        if (state.rooted.count(variable)) continue;
        const Provenance& p = s.provenance;
        bool pointer = variables_.at(variable).kind == POINTER;
        if (collects && (pointer ? p.bits & HEAP : p.bits & LIVE))
          s.stale.insert({call, COLLECT, -1});
        for (const std::string& array : p.arrays)
          if (has_effect(effects, MOVE + array)) s.stale.insert({call, MOVE + array, -1});
        for (int param : p.params) {
          for (const std::string& tag : effects) s.stale.insert({call, tag, param});
          if (collects) s.stale.insert({call, CONTENT, param});
        }
      }
    }
    for (const VarDecl* variable : refreshed) refresh_content(state.variables[variable]);
    // An out parameter: the callee stores a fresh value.
    for (const VarDecl* variable : written) reset(variable, {LIVE, {}, {}}, state);
  }

  void trace(const Stmt* s, const FlowState& state) const {
    llvm::errs() << "  " << s->getStmtClassName() << " "
                 << location_string(sources_, s->getBeginLoc()) << " |";
    for (const auto& [variable, v] : state.variables) {
      const Provenance& p = v.provenance;
      if (!p.bits && p.arrays.empty() && p.params.empty() && v.stale.empty()) continue;
      llvm::errs() << " " << variable->getName() << "=" << p.bits;
      for (const std::string& array : p.arrays) llvm::errs() << "," << array;
      for (const Origin& origin : v.stale) llvm::errs() << "!" << origin.tag;
      if (state.rooted.count(variable)) llvm::errs() << "r";
    }
    llvm::errs() << "\n";
  }

  void transfer(const CFGBlock& block, FlowState& state, bool report) {
    if (tracing_) llvm::errs() << "block " << block.getBlockID() << (report ? " report\n" : "\n");
    for (const CFGElement& element : block) {
      std::optional<CFGStmt> statement = element.getAs<CFGStmt>();
      if (!statement) continue;
      const Stmt* s = statement->getStmt();
      if (tracing_) trace(s, state);
      if (const auto* reference = dyn_cast<DeclRefExpr>(s)) {
        const auto* variable = dyn_cast<VarDecl>(reference->getDecl());
        auto found = variable ? variables_.find(variable) : variables_.end();
        if (found != variables_.end() && is_use(reference, found->second))
          use(reference, variable, state, report);
      } else if (const auto* c = dyn_cast<CallExpr>(s)) {
        call(c, state, report);
      } else if (const auto* binary = dyn_cast<BinaryOperator>(s)) {
        if (binary->getOpcode() != BO_Assign) continue;
        const Expr* left = binary->getLHS()->IgnoreParens();
        bool element = isa<ArraySubscriptExpr>(left);
        if (element) left = cast<ArraySubscriptExpr>(left)->getBase()->IgnoreParenImpCasts();
        const auto* reference = dyn_cast<DeclRefExpr>(left);
        const auto* variable = reference ? dyn_cast<VarDecl>(reference->getDecl()) : nullptr;
        if (variable == nullptr || !variables_.count(variable)) continue;
        if (element != (variables_.at(variable).kind == VALUE_ARRAY)) continue;
        // Storing one element doesn't refresh the others.
        if (element)
          state.variables[variable].provenance.merge(assigned(variable, binary->getRHS(), state));
        else reset(variable, assigned(variable, binary->getRHS(), state), state);
      } else if (const auto* declaration = dyn_cast<DeclStmt>(s)) {
        for (const Decl* d : declaration->decls()) {
          const auto* variable = dyn_cast<VarDecl>(d);
          if (variable == nullptr || !variables_.count(variable)) continue;
          state.rooted.erase(variable);
          if (const Expr* init = variable->getInit())
            reset(variable, assigned(variable, init, state), state);
          else reset(variable, {}, state);
        }
      }
    }
  }

  Program& program_;
  ASTContext& context_;
  const SourceManager& sources_;
  const FunctionDecl* function_;
  std::string key_;
  std::map<const VarDecl*, Variable> variables_;
  std::map<const VarDecl*, std::set<const VarDecl*>> roots_;
  std::set<std::pair<const VarDecl*, Origin>> reported_;
  bool tracing_ = false;
};

class CheckVisitor : public RecursiveASTVisitor<CheckVisitor> {
public:
  CheckVisitor(Program& program, ASTContext& context) : program_(program), context_(context) {}

  bool VisitFunctionDecl(FunctionDecl* function) {
    const SourceManager& sources = context_.getSourceManager();
    if (!function->doesThisDeclarationHaveABody() || in_system(sources, function->getLocation()))
      return true;
    // Header functions are checked once.
    if (program_.checked.insert(location_string(sources, function->getLocation())).second)
      FunctionChecker(program_, context_, function).run();
    return true;
  }

private:
  Program& program_;
  ASTContext& context_;
};

class Consumer : public ASTConsumer {
public:
  Consumer(Program& program, bool graph) : program_(program), graph_(graph) {}

  void HandleTranslationUnit(ASTContext& context) override {
    if (context.getDiagnostics().hasErrorOccurred()) return;
    if (graph_) GraphVisitor(program_, context).TraverseDecl(context.getTranslationUnitDecl());
    else CheckVisitor(program_, context).TraverseDecl(context.getTranslationUnitDecl());
  }

private:
  Program& program_;
  bool graph_;
};

class Action : public ASTFrontendAction {
public:
  Action(Program& program, bool graph) : program_(program), graph_(graph) {}

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance&, llvm::StringRef) override {
    return std::make_unique<Consumer>(program_, graph_);
  }

private:
  Program& program_;
  bool graph_;
};

class Factory : public FrontendActionFactory {
public:
  Factory(Program& program, bool graph) : program_(program), graph_(graph) {}

  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<Action>(program_, graph_);
  }

private:
  Program& program_;
  bool graph_;
};

// Whether a parameter's sensitivity tag makes a fact's need a bug.
bool matches(const std::string& sensitivity, const std::string& need) {
  if (sensitivity == need) return true;
  llvm::StringRef s(sensitivity), n(need);
  bool store = s.consume_back("/w");
  if (store != n.consume_back("/w")) return false;
  return s == MOVE_ANY && n.starts_with(MOVE);
}

void finish_arguments(Program& program) {
  bool changed;
  do {
    changed = false;
    for (const auto& [from, targets] : program.forwards)
      for (const ParamKey& to : targets) {
        auto found = program.sensitive.find(to);
        if (found == program.sensitive.end()) continue;
        for (const std::string& tag : found->second)
          if (program.sensitive[from].insert(tag).second) {
            changed = true;
            if (!program.sensitive_where.count(from))
              program.sensitive_where[from] = "passed on to '" +
                                              program.functions[to.function].name +
                                              "', which uses it at " + program.sensitive_where[to];
          }
      }
  } while (changed);
  if (getenv("OTIUM_GC_LINT_TRACE_PARAMS"))
    for (const auto& [key, tags] : program.sensitive)
      for (const std::string& tag : tags)
        llvm::errs() << program.functions[key.function].name << " " << key.index << " " << tag
                     << ": " << program.sensitive_where[key] << "\n";
  for (const ArgumentFact& fact : program.facts) {
    auto found = program.sensitive.find(fact.param);
    if (found == program.sensitive.end()) continue;
    std::string effect;
    for (const std::string& sensitivity : found->second)
      for (const std::string& need : fact.needs)
        if (effect.empty() && matches(sensitivity, need))
          effect = describe(llvm::StringRef(need).split('/').first.str());
    if (effect.empty()) continue;
    Diagnostic d;
    d.location = fact.location;
    d.text = fact.message + " is passed to '" + program.functions[fact.param.function].name +
             "', which uses parameter " + std::to_string(fact.param.index + 1) +
             " after a call that " + effect + " [otium-gc-stale-argument]";
    d.notes.push_back({"", "the parameter is used at " + program.sensitive_where[fact.param]});
    program.report(std::move(d));
  }
}

void print_diagnostics(const Program& program) {
  std::vector<const Diagnostic*> sorted;
  for (const Diagnostic& d : program.diagnostics) sorted.push_back(&d);
  // file:line:column, compared numerically.
  auto order = [](const std::string& location) {
    size_t column = location.rfind(':'), line = location.rfind(':', column - 1);
    return std::make_tuple(location.substr(0, line), std::atoi(location.c_str() + line + 1),
                           std::atoi(location.c_str() + column + 1));
  };
  std::stable_sort(sorted.begin(), sorted.end(), [&](const Diagnostic* a, const Diagnostic* b) {
    return order(a->location) < order(b->location);
  });
  for (const Diagnostic* d : sorted) {
    llvm::outs() << d->location << ": warning: " << d->text << "\n";
    for (const auto& [location, text] : d->notes)
      llvm::outs() << (location.empty() ? "" : location + ": ") << "note: " << text << "\n";
  }
}

std::string effect_list(const Effects& effects) {
  std::string out;
  for (const std::string& tag : effects) out += (out.empty() ? "" : ", ") + describe(tag);
  return out;
}

void print_effects(const Program& program) {
  for (const auto& [key, node] : program.functions) {
    bool wanted = why.empty() ? list_effects.getValue()
                              : std::find(why.begin(), why.end(), node.name) != why.end();
    if (!wanted) continue;
    if (node.effects.empty()) {
      if (!why.empty()) llvm::outs() << node.name << ": no effects\n";
      continue;
    }
    llvm::outs() << node.name << ": " << effect_list(node.effects) << "\n";
    if (!why.empty())
      for (const std::string& tag : node.effects)
        llvm::outs() << "  " << describe(tag) << ": " << chain(program, key, tag) << "\n";
  }
  if (!why.empty()) return;
  llvm::outs() << "\nFunctions that return pointers into managed objects:\n";
  for (const auto& [key, node] : program.functions)
    if (node.derives && node.defined) llvm::outs() << "  " << node.name << "\n";
  llvm::outs() << "\nIndirect calls assumed to do anything:\n";
  for (const auto& [where, name] : program.indirect_calls)
    llvm::outs() << where << ": " << name << "\n";
}

}  // namespace

int main(int argc, const char** argv) {
  auto options = CommonOptionsParser::create(argc, argv, category);
  if (!options) {
    llvm::errs() << llvm::toString(options.takeError());
    return 2;
  }
  Program program;
  for (const std::string& path : config_paths)
    if (!load_config(path, program.config)) return 2;
  ClangTool tool(options->getCompilations(), options->getSourcePathList());
  // Compiler warnings are not this tool's business.
  tool.appendArgumentsAdjuster(
      getInsertArgumentAdjuster({"-w", "-Wno-error"}, ArgumentInsertPosition::END));
  Factory graph(program, true);
  if (int result = tool.run(&graph)) return result;
  finish_graph(program);
  if (list_effects || !why.empty()) {
    print_effects(program);
    return 0;
  }
  Factory check(program, false);
  if (int result = tool.run(&check)) return result;
  finish_arguments(program);
  print_diagnostics(program);
  llvm::errs() << "otium-gc-lint: checked " << program.checked.size() << " functions, "
               << program.diagnostics.size() << " warnings\n";
  return warnings_as_errors && !program.diagnostics.empty() ? 1 : 0;
}
