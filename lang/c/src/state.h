// state.h — the State: heap, intern table, value stack (GC root), namespace
// registry, unwind state, handler/restart/param stacks.
#pragma once
#include "common.h"
#include "vec.h"
#include "value.h"
#include "heap.h"
#include "intern.h"

typedef struct StateConfig {
  u32 heapBytes;     // default 4 MiB
  u32 stackSlots;    // default 4096
  u32 maxDepth;      // default 512
  u32 heapMaxBytes;  // default 64 MiB
} StateConfig;

static inline StateConfig state_config_default(void) {
  return (StateConfig){
      .heapBytes = 4u * 1024 * 1024,
      .stackSlots = 4096,
      .maxDepth = 512,
      .heapMaxBytes = OT_HEAP_MAX_DEFAULT,
  };
}

typedef void (*WriteFn)(void* ud, const char* s, u32 n);
typedef bool (*LoadFn)(void* ud, const char* nsName, Buf* srcOut);
typedef void (*NativeModuleInit)(State* vm);

// What kind of unwind is in flight when a Tag_Unwind value propagates.
typedef enum UnwindKind : u8 {
  UnwindKind_None,
  UnwindKind_Condition,
  UnwindKind_Restart,
  UnwindKind_Quit,
} UnwindKind;

// The handlers/restarts/paramBindings vectors are traced directly by the
// root walker (state_walk_roots in state.c); entries need no extra stack rooting
// once pushed.
typedef struct HandlerBinding {
  Value pred, handler;
} HandlerBinding;
typedef struct RestartRec {
  Value restart;  // Tag_Restart value
} RestartRec;
typedef struct ParamBinding {
  Value param, value;
} ParamBinding;
typedef struct NativeModule {
  u32 nameSym;
  NativeModuleInit init;
  bool initialized;
} NativeModule;

typedef struct CallFrame {
  Value fn;       // roots the closure and its Code object
  u32 ip;         // byte offset in fn's pinned code
  u32 callBase;   // callee slot; result replaces callee + arguments
  u32 base;       // first local slot
  u32 stackBase;  // first operand slot
  u32 savedNs;
  bool restoreNs;
} CallFrame;

OT_VEC_TYPE(HandlerBinding, VecHandlerBinding);
OT_VEC_TYPE(RestartRec, VecRestartRec);
OT_VEC_TYPE(ParamBinding, VecParamBinding);
OT_VEC_TYPE(CallFrame, VecCallFrame);
OT_VEC_TYPE(NativeModule, VecNativeModule);

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

typedef struct Syms {
#define OT_SYM_FIELD(field, text) u32 field;
  OT_SYM_LIST(OT_SYM_FIELD)
#undef OT_SYM_FIELD
} Syms;

struct State {
  Heap heap;  // must remain the first data member (heap_of relies on it)
  Intern intern;
  VecValue stack;  // GC root: the value stack
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

  // Unwind state, valid while a Tag_Unwind value is propagating. The Value
  // fields here and in handlers/restarts/paramBindings are GC roots alongside
  // the stack and namespace registry.
  UnwindKind unwindKind;
  Value unwindCondition;
  u64 unwindRestartId;      // target restart when unwindKind==Restart
  Value unwindRestartArgs;  // list of args for the restart clause

  VecHandlerBinding handlers;     // innermost last
  u32 handlerVisible;             // handlers[i] visible to signal iff i < this
  VecRestartRec restarts;         // innermost last
  VecParamBinding paramBindings;  // innermost last
  VecU32 loadingNs;               // require cycle detection
  VecCallFrame frames;            // compiled calls; innermost last
  VecNativeModule nativeModules;  // statically linked optional modules

  Syms syms;
};

State* state_create(const StateConfig* cfg);  // NULL cfg = defaults
void state_destroy(State* vm);

// native calling convention: args at stack[base..base+argc)
static inline u32 state_push(State* vm, Value v) {
  vec_push(&vm->stack, v);
  return vm->stack.len - 1;
}
static inline void state_pop_to(State* vm, u32 base) { vm->stack.len = base; }

// --- rooted-slot handles ----------------------------------------------------
//
// A raw heap Value in a C local goes stale at the next allocating call
// (semispace collect moves everything). Internal code therefore keeps heap
// values in rooted vm->stack slots and reads them through Slot at point of
// use; a raw Value may exist only transiently between a slot_get and its use,
// with no allocation in between. Returning a Value is safe when the caller
// immediately roots or returns it.
//
// Slot is a stable name for a stack cell: reads/writes always go through
// vm->stack, so the collector's forwarding is picked up for free. (It must
// never cache a Value* — the stack vec reallocs on push.)
typedef struct Slot {
  State* vm;
  u32 idx;
} Slot;
static inline Value slot_get(Slot s) { return vec_at(&s.vm->stack, s.idx); }
static inline void slot_set(Slot s, Value v) { vec_at(&s.vm->stack, s.idx) = v; }

// Scope replaces the C++ RAII guard: scope_begin snapshots the stack length,
// and EVERY exit from the region must restore it — normal returns go through
// scope_exit(vm, sc, result), unwind propagation goes through OT_TRYS(vm, sc, e).
// Nest strictly: a scope must not outlive values pushed after it by other
// means it doesn't know about.
static inline u32 scope_begin(State* vm) { return vm->stack.len; }
// Guarded because an enclosing VM unwind may already have popped below base.
static inline void scope_pop_to(State* vm, u32 base) {
  if (vm->stack.len > base) vm->stack.len = base;
}
static inline Value scope_exit(State* vm, u32 base, Value result) {
  scope_pop_to(vm, base);
  return result;
}
static inline Slot scope_push(State* vm, Value v) { return (Slot){vm, state_push(vm, v)}; }
static inline Slot scope_slot(State* vm, u32 base, u32 i) {  // i-th push of this scope
  return (Slot){vm, base + i};
}

// Slot-taking constructor sugar: operands are read from their rooted slots
// at call time (the underlying helpers root their Value args internally via
// heap tempRoots, so the raw forms are also safe — these exist so call
// sites don't need a raw Value at all).
static inline Value make_pair_slots(State* vm, Slot car, Slot cdr) {
  return make_pair(vm, slot_get(car), slot_get(cdr));
}
static inline Value make_string_from_slot(State* vm, Slot src, u32 byteOff, u32 len) {
  return make_string_from(vm, slot_get(src), byteOff, len);
}

// Build {:type 'error :message <formatted>}, signal it through active
// handlers, and (if all decline) start a condition unwind. Returns Unwind.
Value raise_error(State* vm, const char* fmt, ...);

// Format an interned name into an error message at a `%.*s` placeholder.
Value raise_error_sym(State* vm, const char* fmt, u32 symId);

// Signal-site walk (spec 8.2). Handlers run innermost-out with the stack
// intact; a handler and everything inner to it is invisible while it runs.
// If all decline: starts an unwind when unwindIfUnhandled, else returns nil.
Value signal_value(State* vm, Value condition, bool unwindIfUnhandled);

// Host-side handler installation (the REPL's interactive restart chooser).
// pred and handler are callable Values; the binding is GC-rooted by the
// handlers vector itself. Push/pop must nest.
Value state_push_handler(State* vm, Value pred, Value handler);
void state_pop_handler(State* vm);

// Sanctioned cancel for an in-flight unwind (e.g. macroexpand probing a
// head symbol that doesn't resolve). Clears all unwind state.
void state_cancel_unwind(State* vm);

// Register a statically linked module for resolution by `require`. The init
// callback runs once with currentNs pre-switched to the module namespace.
void register_native_module(State* vm, const char* name, NativeModuleInit init);
NativeModule* find_native_module(State* vm, u32 nameSym);
