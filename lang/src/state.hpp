// state.hpp — the State: heap, intern table, value stack (GC root), namespace
// registry, unwind state, handler/restart/param stacks.
#pragma once
#include "common.hpp"
#include "vec.hpp"
#include "value.hpp"
#include "heap.hpp"
#include "intern.hpp"

namespace ot {

struct StateConfig {
  u32 heapBytes = 4u * 1024 * 1024;
  u32 stackSlots = 4096;
  u32 maxDepth = 512;
  u32 heapMaxBytes = 64u * 1024 * 1024;
};

using WriteFn = void (*)(void* ud, const char* s, u32 n);
using LoadFn = bool (*)(void* ud, const char* nsName, Buf* srcOut);
using NativeModuleInit = void (*)(State& vm);

// What kind of unwind is in flight when a Tag::Unwind value propagates.
enum class UnwindKind : u8 { None, Condition, Restart, Quit };

// The handlers/restarts/paramBindings vectors are traced directly by the
// root walker (state_walk_roots in state.cpp); entries need no extra stack rooting
// once pushed.
struct HandlerBinding {
  Value pred, handler;
};
struct RestartRec {
  Value restart;
};  // Tag::Restart value
struct ParamBinding {
  Value param, value;
};
struct NativeModule {
  u32 nameSym;
  NativeModuleInit init;
  bool initialized;
};

struct CallFrame {
  Value fn;       // roots the closure and its Code object
  u32 ip;         // byte offset in fn's pinned code
  u32 callBase;   // callee slot; result replaces callee + arguments
  u32 base;       // first local slot
  u32 stackBase;  // first operand slot
  u32 savedNs;
  bool restoreNs;
};

// Pre-interned symbol/keyword name ids. This list generates both storage and
// initialization so adding a field cannot leave it silently uninitialized.
#define OT_SYM_LIST(X)                                                                             \
  X(quote_, "quote")                                                                              \
  X(if_, "if")                                                                                    \
  X(define_, "define")                                                                            \
  X(def_, "def")                                                                                  \
  X(definePriv_, "define-")                                                                       \
  X(setBang_, "set!")                                                                             \
  X(lambda_, "lambda")                                                                            \
  X(fn_, "fn")                                                                                    \
  X(defmacro_, "defmacro")                                                                        \
  X(begin_, "begin")                                                                              \
  X(do_, "do")                                                                                    \
  X(let_, "let")                                                                                  \
  X(while_, "while")                                                                              \
  X(and_, "and")                                                                                  \
  X(or_, "or")                                                                                    \
  X(cond_, "cond")                                                                                \
  X(else_, "else")                                                                                \
  X(quasiquote_, "quasiquote")                                                                    \
  X(unquote_, "unquote")                                                                          \
  X(unquoteSplicing_, "unquote-splicing")                                                         \
  X(ns_, "ns")                                                                                    \
  X(inNs_, "in-ns")                                                                               \
  X(require_, "require")                                                                          \
  X(handlerBind_, "handler-bind")                                                                 \
  X(restartCase_, "restart-case")                                                                 \
  X(try_, "try")                                                                                  \
  X(catch_, "catch")                                                                              \
  X(unwindProtect_, "unwind-protect")                                                             \
  X(defer_, "defer")                                                                              \
  X(defparam_, "defparam")                                                                        \
  X(withParams_, "with-params")                                                                   \
  X(array_, "array")                                                                              \
  X(table_, "table")                                                                              \
  X(amp_, "&")                                                                                    \
  X(otiumCore_, "otium.core")                                                                     \
  X(user_, "user")                                                                                \
  X(expander_, "*expander*")                                                                      \
  X(error_, "error")                                                                              \
  X(quit_, "quit")                                                                                \
  X(kwType, "type")                                                                               \
  X(kwMessage, "message")                                                                         \
  X(kwData, "data")                                                                               \
  X(kwName, "name")                                                                               \
  X(kwVars, "vars")                                                                               \
  X(kwAliases, "aliases")                                                                         \
  X(kwRefers, "refers")                                                                           \
  X(kwOrder, "order")                                                                             \
  X(kwAs, "as")                                                                                   \
  X(kwRefer, "refer")                                                                             \
  X(kwReload, "reload")                                                                           \
  X(kwRequire, "require")

struct Syms {
#define OT_SYM_FIELD(field, text) u32 field;
  OT_SYM_LIST(OT_SYM_FIELD)
#undef OT_SYM_FIELD
};

struct State {
  Heap heap;
  Intern intern;
  Vec<Value> stack;  // GC root: the value stack
  StateConfig cfg;

  Value nsRegistry;   // table nsName -> ns record (also in stack[0])
  Value typeParents;  // condition type registry (also in stack[1])
  u32 currentNs;      // intern id of the current namespace name
  u32 expandNs;       // ns of the form being expanded (0 = none);
                      // the macro oracle resolves against this,
                      // since applying the expander closure
                      // switches currentNs to its defining ns
  u64 gensymCounter;
  u64 restartIdCounter;
  volatile bool interruptFlag;
  WriteFn writeFn;
  void* writeUd;
  LoadFn loadFn;
  void* loadUd;

  // Unwind state, valid while a Tag::Unwind value is propagating. The Value
  // fields here and in handlers/restarts/paramBindings are GC roots alongside
  // the stack and namespace registry.
  UnwindKind unwindKind;
  Value unwindCondition;
  u64 unwindRestartId;      // target restart when unwindKind==Restart
  Value unwindRestartArgs;  // list of args for the restart clause

  Vec<HandlerBinding> handlers;     // innermost last
  u32 handlerVisible;               // handlers[i] visible to signal iff i < this
  Vec<RestartRec> restarts;         // innermost last
  Vec<ParamBinding> paramBindings;  // innermost last
  Vec<u32> loadingNs;               // require cycle detection
  Vec<CallFrame> frames;            // compiled calls; innermost last
  Vec<NativeModule> nativeModules;  // statically linked optional modules

  Syms syms;

  static State* create(const StateConfig&);
  void destroy();

  // native calling convention: args at stack[base..base+argc)
  u32 push(Value v) {
    stack.push(v);
    return stack.len - 1;
  }
  void popTo(u32 base) { stack.len = base; }

  explicit State(const StateConfig&);
  ~State();
};

// --- rooted-slot handles ----------------------------------------------------
//
// A raw heap Value in a C++ local goes stale at the next allocating call
// (semispace collect moves everything). Internal code therefore keeps heap
// values in rooted vm.stack slots and reads them through Slot at point of
// use; a raw Value may exist only transiently between a get() and its use,
// with no allocation in between. Returning a Value is safe when the caller
// immediately roots or returns it.
//
// Slot is a stable name for a stack cell: reads/writes always go through
// vm.stack, so the collector's forwarding is picked up for free. (It must
// never cache a Value* — Vec reallocs on push.)
struct Slot {
  State* vm;
  u32 idx;
  Value get() const { return vm->stack[idx]; }
  void set(Value v) const { vm->stack[idx] = v; }
};

// Scope pairs a run of pushes with the popTo that balances them, surviving
// early returns (which is what makes OT_TRY safe inside a Scope'd frame).
// Nest strictly: a Scope must not outlive values pushed after it by other
// means it doesn't know about.
struct Scope {
  State& vm;
  u32 base;
  explicit Scope(State& v) : vm(v), base(v.stack.len) {}
  // Guarded because an enclosing VM unwind may already have popped below base.
  ~Scope() {
    if (vm.stack.len > base) vm.popTo(base);
  }
  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
  Slot push(Value v = nil_v()) { return Slot{&vm, vm.push(v)}; }
  Slot slot(u32 i) { return Slot{&vm, base + i}; }  // i-th push of this scope
};

// Slot-taking constructor sugar: operands are read from their rooted slots
// at call time (the underlying helpers root their Value args internally via
// Heap::tempRoots, so the raw forms are also safe — these exist so call
// sites don't need a raw Value at all).
inline Value make_pair(State& vm, Slot car, Slot cdr) {
  return make_pair(vm, car.get(), cdr.get());
}
inline Value make_string_from(State& vm, Slot src, u32 byteOff, u32 len) {
  return make_string_from(vm, src.get(), byteOff, len);
}

// Build {:type 'error :message <formatted>}, signal it through active
// handlers, and (if all decline) start a condition unwind. Returns Unwind.
Value raise_error(State&, const char* fmt, ...);

// Format an interned name into an error message at a `%.*s` placeholder.
Value raise_error_sym(State&, const char* fmt, u32 symId);

// Signal-site walk (spec 8.2). Handlers run innermost-out with the stack
// intact; a handler and everything inner to it is invisible while it runs.
// If all decline: starts an unwind when unwindIfUnhandled, else returns nil.
Value signal_value(State&, Value condition, bool unwindIfUnhandled);

// Host-side handler installation (the REPL's interactive restart chooser).
// pred and handler are callable Values; the binding is GC-rooted by the
// handlers vector itself. Push/pop must nest.
Value state_push_handler(State&, Value pred, Value handler);
void state_pop_handler(State&);

// Sanctioned cancel for an in-flight unwind (e.g. macroexpand probing a
// head symbol that doesn't resolve). Clears all unwind state.
void state_cancel_unwind(State&);

// Register a statically linked module for resolution by `require`. The init
// callback runs once with currentNs pre-switched to the module namespace.
void register_native_module(State&, const char* name, NativeModuleInit init);
NativeModule* find_native_module(State&, u32 nameSym);

}  // namespace ot
