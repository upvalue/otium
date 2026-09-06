#include "clang/AST/Attr.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendAction.h"
#include "clang/Analysis/CFG.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Error.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace clang;
using namespace clang::tooling;

namespace {

llvm::cl::OptionCategory gc_lint_category("otium-gc-lint options");
llvm::cl::opt<bool>
    warnings_as_errors("warnings-as-errors",
                       llvm::cl::desc("Return failure when the linter reports a warning"),
                       llvm::cl::init(false), llvm::cl::cat(gc_lint_category));
llvm::cl::opt<bool> report_suppressions("report-suppressions",
                                        llvm::cl::desc("Report each accepted GC lint escape hatch"),
                                        llvm::cl::init(false), llvm::cl::cat(gc_lint_category));

constexpr llvm::StringLiteral may_allocate_annotation = "otium.gc.may_allocate";
constexpr llvm::StringLiteral stale_borrow_annotation = "otium.gc.allow_stale_borrow:";
constexpr llvm::StringLiteral direct_store_annotation = "otium.gc.allow_direct_store:";

struct FunctionInfo {
  const FunctionDecl* definition = nullptr;
  std::vector<const FunctionDecl*> callees;
  bool calls_indirectly = false;
  bool may_allocate = false;
};

using FunctionMap = std::unordered_map<const FunctionDecl*, FunctionInfo>;

bool has_annotation(const FunctionDecl* function, llvm::StringRef annotation) {
  for (const FunctionDecl* redeclaration : function->redecls())
    for (const Attr* attribute : redeclaration->attrs())
      if (const auto* annotated = dyn_cast<AnnotateAttr>(attribute))
        if (annotated->getAnnotation() == annotation) return true;
  return false;
}

std::optional<std::string> stale_borrow_reason(const VarDecl* variable) {
  for (const Attr* attribute : variable->attrs()) {
    const auto* annotated = dyn_cast<AnnotateAttr>(attribute);
    if (annotated == nullptr) continue;
    llvm::StringRef annotation = annotated->getAnnotation();
    if (annotation.consume_front(stale_borrow_annotation)) return annotation.str();
  }
  return std::nullopt;
}

std::optional<std::string> direct_store_reason(const FunctionDecl* function) {
  for (const FunctionDecl* redeclaration : function->redecls()) {
    for (const Attr* attribute : redeclaration->attrs()) {
      const auto* annotated = dyn_cast<AnnotateAttr>(attribute);
      if (annotated == nullptr) continue;
      llvm::StringRef annotation = annotated->getAnnotation();
      if (annotation.consume_front(direct_store_annotation)) return annotation.str();
    }
  }
  return std::nullopt;
}

bool is_allocator_name(llvm::StringRef name) { return name == "ot_alloc" || name == "ot_collect"; }

class FunctionCollector : public RecursiveASTVisitor<FunctionCollector> {
public:
  explicit FunctionCollector(FunctionMap& functions) : functions_(functions) {}

  bool TraverseFunctionDecl(FunctionDecl* function) {
    const FunctionDecl* old_function = current_function_;
    const FunctionDecl* canonical = function->getCanonicalDecl();
    FunctionInfo& info = functions_[canonical];
    if (function->doesThisDeclarationHaveABody()) {
      info.definition = function;
      current_function_ = canonical;
    }
    if (is_allocator_name(function->getName()) || has_annotation(function, may_allocate_annotation))
      info.may_allocate = true;
    bool result = RecursiveASTVisitor<FunctionCollector>::TraverseFunctionDecl(function);
    current_function_ = old_function;
    return result;
  }

  bool VisitCallExpr(CallExpr* call) {
    if (current_function_ == nullptr) return true;
    FunctionInfo& info = functions_[current_function_];
    if (const FunctionDecl* callee = call->getDirectCallee())
      info.callees.push_back(callee->getCanonicalDecl());
    else info.calls_indirectly = true;
    return true;
  }

private:
  FunctionMap& functions_;
  const FunctionDecl* current_function_ = nullptr;
};

void finish_function_summaries(FunctionMap& functions) {
  bool changed;
  do {
    changed = false;
    for (auto& [function, info] : functions) {
      if (info.may_allocate) continue;
      bool may_allocate = info.calls_indirectly;
      for (const FunctionDecl* callee : info.callees) {
        auto found = functions.find(callee);
        if ((found != functions.end() && found->second.may_allocate) ||
            has_annotation(callee, may_allocate_annotation) ||
            is_allocator_name(callee->getName())) {
          may_allocate = true;
          break;
        }
      }
      if (may_allocate) {
        info.may_allocate = true;
        changed = true;
      }
    }
  } while (changed);
}

bool is_gc_object_pointer(QualType type) {
  const auto* pointer = type->getAs<PointerType>();
  if (pointer == nullptr) return false;
  const auto* record = pointer->getPointeeType()->getAs<RecordType>();
  if (record == nullptr) return false;
  llvm::StringRef name = record->getDecl()->getName();
  return name == "ot_obj" || (name.starts_with("ot_") && name.ends_with("_obj"));
}

struct Event {
  enum Kind { Use, Safepoint, Reset } kind;
  unsigned offset;
  SourceLocation location;
  const VarDecl* variable = nullptr;
  const CallExpr* call = nullptr;
};

struct FlowState {
  bool reachable = false;
  std::set<const VarDecl*> active;
  std::map<const VarDecl*, const CallExpr*> invalidated_by;

  bool operator==(const FlowState& other) const {
    return reachable == other.reachable && active == other.active &&
           invalidated_by == other.invalidated_by;
  }
};

class EventCollector : public RecursiveASTVisitor<EventCollector> {
public:
  EventCollector(const SourceManager& sources, const FunctionMap& functions)
      : sources_(sources), functions_(functions) {}

  bool VisitVarDecl(VarDecl* variable) {
    if (!variable->isLocalVarDecl() || !is_gc_object_pointer(variable->getType())) return true;
    const VarDecl* canonical = variable->getCanonicalDecl();
    tracked_.insert(canonical);
    SourceLocation end = sources_.getExpansionLoc(variable->getEndLoc());
    if (end.isValid() && end.isFileID())
      initialization_ends_[canonical] = sources_.getFileOffset(end);
    add_event(Event::Reset, variable->getEndLoc(), canonical, nullptr);
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator* operation) {
    if (!operation->isAssignmentOp()) return true;
    const Expr* left = operation->getLHS()->IgnoreParenImpCasts();
    const auto* reference = dyn_cast<DeclRefExpr>(left);
    if (reference == nullptr) return true;
    const auto* variable = dyn_cast<VarDecl>(reference->getDecl());
    if (variable == nullptr || !is_gc_object_pointer(variable->getType())) return true;
    writes_.insert(reference);
    add_event(Event::Reset, operation->getEndLoc(), variable->getCanonicalDecl(), nullptr);
    return true;
  }

  bool VisitDeclRefExpr(DeclRefExpr* reference) {
    const auto* variable = dyn_cast<VarDecl>(reference->getDecl());
    if (variable == nullptr || !is_gc_object_pointer(variable->getType())) return true;
    references_.push_back(reference);
    return true;
  }

  bool VisitCallExpr(CallExpr* call) {
    if (!may_allocate(call)) return true;
    add_event(Event::Safepoint, call->getEndLoc(), nullptr, call);
    return true;
  }

  std::vector<Event> take_events() {
    for (const DeclRefExpr* reference : references_) {
      if (writes_.count(reference) != 0) continue;
      const auto* variable = cast<VarDecl>(reference->getDecl())->getCanonicalDecl();
      SourceLocation location = sources_.getExpansionLoc(reference->getExprLoc());
      auto initialization_end = initialization_ends_.find(variable);
      if (location.isValid() && location.isFileID() &&
          initialization_end != initialization_ends_.end() &&
          sources_.getFileOffset(location) <= initialization_end->second)
        continue;
      add_event(Event::Use, reference->getExprLoc(), variable, nullptr);
    }
    std::sort(events_.begin(), events_.end(), [](const Event& left, const Event& right) {
      if (left.offset != right.offset) return left.offset < right.offset;
      return left.kind < right.kind;
    });
    return std::move(events_);
  }

private:
  bool may_allocate(const CallExpr* call) const {
    const FunctionDecl* callee = call->getDirectCallee();
    if (callee == nullptr) return true;
    callee = callee->getCanonicalDecl();
    auto found = functions_.find(callee);
    return (found != functions_.end() && found->second.may_allocate) ||
           has_annotation(callee, may_allocate_annotation) || is_allocator_name(callee->getName());
  }

  void add_event(Event::Kind kind, SourceLocation location, const VarDecl* variable,
                 const CallExpr* call) {
    location = sources_.getExpansionLoc(location);
    if (location.isInvalid() || !location.isFileID()) return;
    events_.push_back({kind, sources_.getFileOffset(location), location, variable, call});
  }

  const SourceManager& sources_;
  const FunctionMap& functions_;
  std::unordered_set<const VarDecl*> tracked_;
  std::unordered_map<const VarDecl*, unsigned> initialization_ends_;
  std::unordered_set<const DeclRefExpr*> writes_;
  std::vector<const DeclRefExpr*> references_;
  std::vector<Event> events_;
};

using OwnerSet = std::set<const RecordDecl*>;

class ManagedLayout {
public:
  explicit ManagedLayout(ASTContext& context) : context_(context) {}

  void build(TranslationUnitDecl* unit) {
    std::vector<const RecordDecl*> roots;
    for (Decl* declaration : unit->decls()) {
      const auto* record = dyn_cast<RecordDecl>(declaration);
      if (record == nullptr || !record->isCompleteDefinition()) continue;
      llvm::StringRef name = record->getName();
      if (name == "ot_obj" || (name.starts_with("ot_") && name.ends_with("_obj")))
        roots.push_back(cast<RecordDecl>(record->getCanonicalDecl()));
    }
    for (const RecordDecl* root : roots) {
      std::set<const RecordDecl*> visiting;
      collect_record(root, root, visiting);
    }
  }

  const OwnerSet* edge_owners(const FieldDecl* field) const {
    auto found = edge_owners_.find(field->getCanonicalDecl());
    return found == edge_owners_.end() ? nullptr : &found->second;
  }

  const OwnerSet* record_owners(const RecordDecl* record) const {
    auto found = record_owners_.find(cast<RecordDecl>(record->getCanonicalDecl()));
    return found == record_owners_.end() ? nullptr : &found->second;
  }

  const RecordDecl* pointed_record(QualType type) const {
    if (const auto* pointer = type->getAs<PointerType>()) type = pointer->getPointeeType();
    const auto* record = type->getAs<RecordType>();
    return record == nullptr ? nullptr : cast<RecordDecl>(record->getDecl()->getCanonicalDecl());
  }

  bool is_otv_pointer(QualType type) const {
    const auto* pointer = type->getAs<PointerType>();
    return pointer != nullptr && is_otv(pointer->getPointeeType());
  }

private:
  bool is_otv(QualType type) const {
    type = type.getUnqualifiedType();
    const auto* alias = type->getAs<TypedefType>();
    return alias != nullptr && alias->getDecl()->getName() == "otv";
  }

  QualType array_element(QualType type) const {
    const ArrayType* array = context_.getAsArrayType(type);
    return array == nullptr ? QualType() : array->getElementType();
  }

  bool collect_record(const RecordDecl* record, const RecordDecl* owner,
                      std::set<const RecordDecl*>& visiting) {
    record = record->getDefinition();
    if (record == nullptr) return false;
    record = cast<RecordDecl>(record->getCanonicalDecl());
    owner = cast<RecordDecl>(owner->getCanonicalDecl());
    if (!visiting.insert(record).second) return false;
    bool has_edges = false;
    for (const FieldDecl* field : record->fields()) {
      QualType type = field->getType();
      QualType element = array_element(type);
      if (is_otv(type) || (!element.isNull() && is_otv(element))) {
        edge_owners_[field->getCanonicalDecl()].insert(owner);
        has_edges = true;
        continue;
      }
      if (!element.isNull()) type = element;
      const auto* nested_type = type->getAs<RecordType>();
      if (nested_type != nullptr &&
          collect_record(cast<RecordDecl>(nested_type->getDecl()->getCanonicalDecl()), owner,
                         visiting))
        has_edges = true;
    }
    visiting.erase(record);
    if (has_edges) record_owners_[record].insert(owner);
    return has_edges;
  }

  ASTContext& context_;
  std::unordered_map<const FieldDecl*, OwnerSet> edge_owners_;
  std::unordered_map<const RecordDecl*, OwnerSet> record_owners_;
};

class WriteBarrierChecker : public RecursiveASTVisitor<WriteBarrierChecker> {
public:
  WriteBarrierChecker(ASTContext& context, const ManagedLayout& layout, unsigned& warning_count)
      : context_(context), sources_(context.getSourceManager()),
        diagnostics_(context.getDiagnostics()), layout_(layout), warning_count_(warning_count) {}

  void check(const FunctionDecl* function) {
    current_function_ = function;
    aliases_.clear();
    suppression_reported_ = false;
    TraverseStmt(const_cast<Stmt*>(function->getBody()));
    current_function_ = nullptr;
  }

  bool VisitVarDecl(VarDecl* variable) {
    if (!variable->isLocalVarDecl() || variable->getInit() == nullptr ||
        !layout_.is_otv_pointer(variable->getType()))
      return true;
    if (const OwnerSet* owners = owners_for_edge_expr(variable->getInit()))
      aliases_[variable->getCanonicalDecl()] = *owners;
    return true;
  }

  bool VisitBinaryOperator(BinaryOperator* operation) {
    if (!operation->isAssignmentOp() || !in_main_file(operation->getOperatorLoc())) return true;
    const Expr* left = operation->getLHS();
    if (owners_for_written_edge(left) != nullptr || owners_for_record_value_expr(left) != nullptr)
      report_direct_store(operation->getOperatorLoc());

    const Expr* stripped = strip_expr(left);
    if (const auto* reference = dyn_cast<DeclRefExpr>(stripped)) {
      const auto* variable = dyn_cast<VarDecl>(reference->getDecl());
      if (variable != nullptr && layout_.is_otv_pointer(variable->getType())) {
        if (const OwnerSet* owners = owners_for_edge_expr(operation->getRHS()))
          aliases_[variable->getCanonicalDecl()] = *owners;
        else aliases_.erase(variable->getCanonicalDecl());
      }
    }
    return true;
  }

  bool VisitUnaryOperator(UnaryOperator* operation) {
    if (!operation->isIncrementDecrementOp() || !in_main_file(operation->getOperatorLoc()))
      return true;
    if (owners_for_edge_expr(operation->getSubExpr()) != nullptr)
      report_direct_store(operation->getOperatorLoc());
    return true;
  }

  bool VisitCallExpr(CallExpr* call) {
    if (!in_main_file(call->getExprLoc())) return true;
    const FunctionDecl* callee = call->getDirectCallee();
    if (callee == nullptr) return true;
    llvm::StringRef name = callee->getName();
    if (name == "ot_store") {
      check_ot_store(call);
      return true;
    }
    if ((name == "memcpy" || name == "memmove" || name == "memset") && call->getNumArgs() > 0 &&
        destination_is_managed(call->getArg(0)))
      report_bulk_store(call->getExprLoc(), name);
    return true;
  }

private:
  const Expr* strip_expr(const Expr* expression) const {
    while (expression != nullptr) {
      expression = expression->IgnoreParens();
      const auto* cast = dyn_cast<CastExpr>(expression);
      if (cast == nullptr) break;
      expression = cast->getSubExpr();
    }
    return expression;
  }

  const OwnerSet* owners_for_edge_expr(const Expr* expression) const {
    expression = strip_expr(expression);
    if (expression == nullptr) return nullptr;
    if (const auto* member = dyn_cast<MemberExpr>(expression)) {
      const auto* field = dyn_cast<FieldDecl>(member->getMemberDecl());
      return field == nullptr ? nullptr : layout_.edge_owners(field);
    }
    if (const auto* subscript = dyn_cast<ArraySubscriptExpr>(expression))
      return owners_for_edge_expr(subscript->getBase());
    if (const auto* unary = dyn_cast<UnaryOperator>(expression))
      return owners_for_edge_expr(unary->getSubExpr());
    if (const auto* reference = dyn_cast<DeclRefExpr>(expression)) {
      const auto* variable = dyn_cast<VarDecl>(reference->getDecl());
      if (variable == nullptr) return nullptr;
      auto found = aliases_.find(variable->getCanonicalDecl());
      return found == aliases_.end() ? nullptr : &found->second;
    }
    return nullptr;
  }

  const OwnerSet* owners_for_written_edge(const Expr* expression) const {
    expression = strip_expr(expression);
    if (expression == nullptr) return nullptr;
    if (const auto* member = dyn_cast<MemberExpr>(expression)) {
      const auto* field = dyn_cast<FieldDecl>(member->getMemberDecl());
      return field == nullptr ? nullptr : layout_.edge_owners(field);
    }
    if (const auto* subscript = dyn_cast<ArraySubscriptExpr>(expression))
      return owners_for_edge_expr(subscript->getBase());
    if (const auto* unary = dyn_cast<UnaryOperator>(expression)) {
      if (unary->getOpcode() == UO_Deref) return owners_for_edge_expr(unary->getSubExpr());
    }
    return nullptr;
  }

  const OwnerSet* owners_for_record_expr(const Expr* expression) const {
    expression = strip_expr(expression);
    if (expression == nullptr) return nullptr;
    const RecordDecl* record = layout_.pointed_record(expression->getType());
    return record == nullptr ? nullptr : layout_.record_owners(record);
  }

  const OwnerSet* owners_for_record_value_expr(const Expr* expression) const {
    expression = strip_expr(expression);
    if (expression == nullptr) return nullptr;
    const auto* record = expression->getType()->getAs<RecordType>();
    return record == nullptr ? nullptr : layout_.record_owners(record->getDecl());
  }

  bool destination_is_managed(const Expr* destination) const {
    return owners_for_edge_expr(destination) != nullptr ||
           owners_for_record_expr(destination) != nullptr;
  }

  const RecordDecl* owner_record(const Expr* expression) const {
    expression = expression == nullptr ? nullptr : expression->IgnoreParenImpCasts();
    return expression == nullptr ? nullptr : layout_.pointed_record(expression->getType());
  }

  void check_ot_store(const CallExpr* call) {
    if (call->getNumArgs() != 4) return;
    const OwnerSet* owners = owners_for_edge_expr(call->getArg(2));
    if (owners == nullptr) {
      report_bad_target(call->getArg(2)->getExprLoc());
      return;
    }
    const RecordDecl* owner = owner_record(call->getArg(1));
    if (owner == nullptr || owners->count(cast<RecordDecl>(owner->getCanonicalDecl())) == 0)
      report_bad_owner(call->getArg(1)->getExprLoc(), owner);
  }

  bool in_main_file(SourceLocation location) const {
    location = sources_.getExpansionLoc(location);
    return location.isValid() && sources_.isWrittenInMainFile(location);
  }

  bool suppress(SourceLocation location) {
    std::optional<std::string> reason = direct_store_reason(current_function_);
    if (!reason || reason->empty()) return false;
    if (report_suppressions && !suppression_reported_) {
      unsigned remark = diagnostics_.getCustomDiagID(
          DiagnosticsEngine::Remark,
          "accepted direct-store escape hatch for '%0': %1 [otium-gc-write-barrier]");
      diagnostics_.Report(sources_.getExpansionLoc(location), remark)
          << current_function_->getName() << *reason;
      suppression_reported_ = true;
    }
    return true;
  }

  void note_empty_suppression() {
    std::optional<std::string> reason = direct_store_reason(current_function_);
    if (!reason || !reason->empty()) return;
    unsigned note = diagnostics_.getCustomDiagID(
        DiagnosticsEngine::Note, "OT_GC_ALLOW_DIRECT_STORE requires a nonempty reason");
    diagnostics_.Report(sources_.getExpansionLoc(current_function_->getLocation()), note);
  }

  void report_direct_store(SourceLocation location) {
    if (suppress(location)) return;
    unsigned warning = diagnostics_.getCustomDiagID(
        DiagnosticsEngine::Warning,
        "managed heap edge is written without ot_store [otium-gc-write-barrier]");
    diagnostics_.Report(sources_.getExpansionLoc(location), warning);
    unsigned note = diagnostics_.getCustomDiagID(
        DiagnosticsEngine::Note, "write the edge with ot_store(state, owner, slot, value)");
    diagnostics_.Report(sources_.getExpansionLoc(location), note);
    note_empty_suppression();
    warning_count_++;
  }

  void report_bulk_store(SourceLocation location, llvm::StringRef name) {
    if (suppress(location)) return;
    unsigned warning = diagnostics_.getCustomDiagID(
        DiagnosticsEngine::Warning,
        "bulk write with '%0' may replace managed heap edges without write barriers "
        "[otium-gc-write-barrier]");
    diagnostics_.Report(sources_.getExpansionLoc(location), warning) << name;
    note_empty_suppression();
    warning_count_++;
  }

  void report_bad_target(SourceLocation location) {
    if (suppress(location)) return;
    unsigned warning = diagnostics_.getCustomDiagID(
        DiagnosticsEngine::Warning,
        "ot_store target is not a managed heap edge [otium-gc-write-barrier]");
    diagnostics_.Report(sources_.getExpansionLoc(location), warning);
    note_empty_suppression();
    warning_count_++;
  }

  void report_bad_owner(SourceLocation location, const RecordDecl* owner) {
    if (suppress(location)) return;
    unsigned warning = diagnostics_.getCustomDiagID(
        DiagnosticsEngine::Warning,
        "ot_store owner type '%0' does not contain the target edge [otium-gc-write-barrier]");
    llvm::StringRef name = owner == nullptr ? "unknown" : owner->getName();
    diagnostics_.Report(sources_.getExpansionLoc(location), warning) << name;
    note_empty_suppression();
    warning_count_++;
  }

  ASTContext& context_;
  const SourceManager& sources_;
  DiagnosticsEngine& diagnostics_;
  const ManagedLayout& layout_;
  unsigned& warning_count_;
  const FunctionDecl* current_function_ = nullptr;
  std::unordered_map<const VarDecl*, OwnerSet> aliases_;
  bool suppression_reported_ = false;
};

class TranslationUnitChecker {
public:
  TranslationUnitChecker(ASTContext& context, unsigned& warning_count)
      : context_(context), sources_(context.getSourceManager()),
        diagnostics_(context.getDiagnostics()), warning_count_(warning_count) {}

  void run() {
    FunctionCollector collector(functions_);
    collector.TraverseDecl(context_.getTranslationUnitDecl());
    finish_function_summaries(functions_);
    ManagedLayout layout(context_);
    layout.build(context_.getTranslationUnitDecl());
    WriteBarrierChecker barrier(context_, layout, warning_count_);
    for (const auto& [function, info] : functions_) {
      if (info.definition == nullptr) continue;
      SourceLocation location = sources_.getExpansionLoc(info.definition->getLocation());
      if (!sources_.isWrittenInMainFile(location)) continue;
      check_function(info.definition);
      barrier.check(info.definition);
    }
  }

private:
  void check_function(const FunctionDecl* function) {
    EventCollector collector(sources_, functions_);
    collector.TraverseStmt(const_cast<Stmt*>(function->getBody()));
    std::vector<Event> events = collector.take_events();
    CFG::BuildOptions options;
    std::unique_ptr<CFG> cfg = CFG::buildCFG(function, function->getBody(), &context_, options);
    if (cfg == nullptr) return;

    std::vector<std::vector<Event>> block_events(cfg->getNumBlockIDs());
    std::vector<unsigned> event_spans(events.size(), std::numeric_limits<unsigned>::max());
    for (const CFGBlock* block : *cfg) {
      for (const CFGElement& element : *block) {
        std::optional<CFGStmt> cfg_statement = element.getAs<CFGStmt>();
        if (!cfg_statement) continue;
        const Stmt* statement = cfg_statement->getStmt();
        SourceLocation begin = sources_.getExpansionLoc(statement->getBeginLoc());
        SourceLocation end = sources_.getExpansionLoc(statement->getEndLoc());
        if (begin.isInvalid() || end.isInvalid() || !begin.isFileID() || !end.isFileID() ||
            sources_.getFileID(begin) != sources_.getFileID(end))
          continue;
        unsigned begin_offset = sources_.getFileOffset(begin);
        unsigned end_offset = sources_.getFileOffset(end);
        if (end_offset < begin_offset) continue;
        unsigned span = end_offset - begin_offset;
        for (size_t i = 0; i < events.size(); i++) {
          if (events[i].offset < begin_offset || events[i].offset > end_offset ||
              span >= event_spans[i])
            continue;
          event_spans[i] = span;
          for (std::vector<Event>& assigned : block_events)
            assigned.erase(std::remove_if(assigned.begin(), assigned.end(),
                                          [&](const Event& event) {
                                            return event.location == events[i].location &&
                                                   event.kind == events[i].kind &&
                                                   event.variable == events[i].variable &&
                                                   event.call == events[i].call;
                                          }),
                           assigned.end());
          block_events[block->getBlockID()].push_back(events[i]);
        }
      }
    }
    for (std::vector<Event>& assigned : block_events)
      std::sort(assigned.begin(), assigned.end(), [](const Event& left, const Event& right) {
        if (left.offset != right.offset) return left.offset < right.offset;
        return left.kind < right.kind;
      });

    std::vector<FlowState> incoming(cfg->getNumBlockIDs());
    std::vector<FlowState> outgoing(cfg->getNumBlockIDs());
    incoming[cfg->getEntry().getBlockID()].reachable = true;
    bool changed;
    do {
      changed = false;
      for (const CFGBlock* block : *cfg) {
        FlowState merged;
        if (block == &cfg->getEntry()) merged.reachable = true;
        for (CFGBlock::const_pred_iterator predecessor = block->pred_begin();
             predecessor != block->pred_end(); ++predecessor) {
          if (*predecessor == nullptr) continue;
          const FlowState& predecessor_state = outgoing[(*predecessor)->getBlockID()];
          if (!predecessor_state.reachable) continue;
          merged.reachable = true;
          merged.active.insert(predecessor_state.active.begin(), predecessor_state.active.end());
          for (const auto& [variable, call] : predecessor_state.invalidated_by)
            merged.invalidated_by.emplace(variable, call);
        }
        FlowState next = merged;
        transfer(block_events[block->getBlockID()], next, nullptr, nullptr);
        unsigned id = block->getBlockID();
        if (!(incoming[id] == merged) || !(outgoing[id] == next)) {
          incoming[id] = std::move(merged);
          outgoing[id] = std::move(next);
          changed = true;
        }
      }
    } while (changed);

    std::set<std::pair<const VarDecl*, const CallExpr*>> reported;
    std::unordered_set<const VarDecl*> reported_suppressions;
    for (const CFGBlock* block : *cfg) {
      unsigned id = block->getBlockID();
      if (!incoming[id].reachable) continue;
      FlowState state = incoming[id];
      transfer(block_events[id], state, &reported, &reported_suppressions);
    }
  }

  void transfer(const std::vector<Event>& events, FlowState& state,
                std::set<std::pair<const VarDecl*, const CallExpr*>>* reported,
                std::unordered_set<const VarDecl*>* reported_suppressions) {
    for (const Event& event : events) {
      if (event.kind == Event::Reset) {
        state.active.insert(event.variable);
        state.invalidated_by.erase(event.variable);
        continue;
      }
      if (event.kind == Event::Safepoint) {
        for (const VarDecl* variable : state.active) state.invalidated_by[variable] = event.call;
        state.active.clear();
        continue;
      }
      auto invalidation = state.invalidated_by.find(event.variable);
      if (invalidation == state.invalidated_by.end() || reported == nullptr) continue;
      std::pair<const VarDecl*, const CallExpr*> diagnostic(event.variable, invalidation->second);
      if (!reported->insert(diagnostic).second) continue;
      if (std::optional<std::string> reason = stale_borrow_reason(event.variable)) {
        if (!reason->empty()) {
          if (report_suppressions && reported_suppressions->insert(event.variable).second)
            report_suppression(event, *reason);
          continue;
        }
      }
      report_stale_borrow(event, invalidation->second);
      if (stale_borrow_reason(event.variable)) {
        unsigned note = diagnostics_.getCustomDiagID(
            DiagnosticsEngine::Note, "OT_GC_ALLOW_STALE_BORROW requires a nonempty reason");
        diagnostics_.Report(sources_.getExpansionLoc(event.variable->getLocation()), note);
      }
    }
  }

  void report_stale_borrow(const Event& use, const CallExpr* call) {
    llvm::StringRef call_name = "indirect call";
    if (const FunctionDecl* callee = call->getDirectCallee()) call_name = callee->getName();
    unsigned warning = diagnostics_.getCustomDiagID(
        DiagnosticsEngine::Warning,
        "raw GC pointer '%0' may be stale after possibly allocating call to '%1' "
        "[otium-gc-stale-borrow]");
    diagnostics_.Report(use.location, warning) << use.variable->getName() << call_name;
    unsigned call_note =
        diagnostics_.getCustomDiagID(DiagnosticsEngine::Note, "possible collection occurs here");
    diagnostics_.Report(sources_.getExpansionLoc(call->getExprLoc()), call_note);
    unsigned declaration_note = diagnostics_.getCustomDiagID(
        DiagnosticsEngine::Note, "rederive this pointer from its rooted otv after the call");
    diagnostics_.Report(sources_.getExpansionLoc(use.variable->getLocation()), declaration_note);
    warning_count_++;
  }

  void report_suppression(const Event& use, llvm::StringRef reason) {
    unsigned remark = diagnostics_.getCustomDiagID(
        DiagnosticsEngine::Remark,
        "accepted stale-borrow escape hatch for '%0': %1 [otium-gc-stale-borrow]");
    diagnostics_.Report(use.location, remark) << use.variable->getName() << reason;
  }

  ASTContext& context_;
  const SourceManager& sources_;
  DiagnosticsEngine& diagnostics_;
  unsigned& warning_count_;
  FunctionMap functions_;
};

class LintConsumer : public ASTConsumer {
public:
  explicit LintConsumer(unsigned& warning_count) : warning_count_(warning_count) {}

  void HandleTranslationUnit(ASTContext& context) override {
    TranslationUnitChecker(context, warning_count_).run();
  }

private:
  unsigned& warning_count_;
};

class LintAction : public ASTFrontendAction {
public:
  explicit LintAction(unsigned& warning_count) : warning_count_(warning_count) {}

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance&, llvm::StringRef) override {
    return std::make_unique<LintConsumer>(warning_count_);
  }

private:
  unsigned& warning_count_;
};

class LintActionFactory : public FrontendActionFactory {
public:
  explicit LintActionFactory(unsigned& warning_count) : warning_count_(warning_count) {}

  std::unique_ptr<FrontendAction> create() override {
    return std::make_unique<LintAction>(warning_count_);
  }

private:
  unsigned& warning_count_;
};

}  // namespace

int main(int argc, const char** argv) {
  auto options = CommonOptionsParser::create(argc, argv, gc_lint_category);
  if (!options) {
    llvm::errs() << llvm::toString(options.takeError());
    return 2;
  }
  ClangTool tool(options->getCompilations(), options->getSourcePathList());
  unsigned warning_count = 0;
  LintActionFactory factory(warning_count);
  int result = tool.run(&factory);
  if (result != 0) return result;
  return warnings_as_errors && warning_count != 0 ? 1 : 0;
}
