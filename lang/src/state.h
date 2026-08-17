// state.h — the State: heap, intern table, value stack (GC root), namespace
// registry, unwind state, handler/restart/param stacks.
#pragma once
#include "common.h"
#include "vec.h"
#include "value.h"
#include "slots.h"
#ifndef OT_HEAP_INTERNALS
#define OT_STATE_HEAP_INCLUDE
#define OT_HEAP_INTERNALS
#endif
#include "heap.h"
#ifdef OT_STATE_HEAP_INCLUDE
#undef OT_HEAP_INTERNALS
#undef OT_STATE_HEAP_INCLUDE
#endif
#include "intern.h"

// Ceilings are error limits, not reservations: the stack and frame vector start
// at the *Initial* sizes and grow on demand up to the *Slots*/*Depth* caps,
// where exceeding them raises a catchable condition rather than aborting. This
// follows Lua, whose LUAI_MAXSTACK is a million slots against a stack that
// starts at a few dozen. An embedded host that wants a known worst case sets
// the caps down to the initial sizes and gets a fixed allocation.
//
// Growth reallocs, so the stack's address is not stable. Rooted handles are
// indices for that reason (see Ref below) -- the same reason Lua's C API
// addresses its stack by index rather than by TValue*.
typedef struct StateConfig {
  u32 heapBytes;      // default 4 MiB
  u32 stackSlots;     // ceiling, default 1 Mi slots (16 MiB if ever reached)
  u32 stackInitial;   // reserved up front, default 4096 slots (64 KiB)
  u32 maxDepth;       // call depth ceiling, default 200000
  u32 framesInitial;  // reserved up front, default 256 frames
  u32 heapMaxBytes;   // default 64 MiB
} StateConfig;

static inline StateConfig state_config_default(void) {
  return (StateConfig){
      .heapBytes = 4u * 1024 * 1024,
      .stackSlots = 1u << 20,
      .stackInitial = 4096,
      .maxDepth = 200000,
      .framesInitial = 256,
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

// Pre-interned symbol/keyword name ids: OT_SYM_LIST and Syms live in
// slots.h so any file can name them through ot_syms.

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
HeapStats state_gc_stats(const State* vm);

// native calling convention: args at stack[base..base+argc)
static inline u32 state_push(State* vm, Value v) {
  vec_push(&vm->stack, v);
  return vm->stack.len - 1;
}
static inline void state_pop_to(State* vm, u32 base) { vm->stack.len = base; }

// --- rooted handles ---------------------------------------------------------
//
// A raw heap Value in a C local goes stale at the next allocating call: the
// collector moves everything. The rule is therefore that a heap value lives on
// the value stack and nowhere else. A raw Value may exist only between a
// ref_get and its immediate use, with no allocating call in between.
//
// A function that can return a heap value returns it ON THE STACK and reports
// control flow through Status:
//
//   Status_Ok      => exactly one value pushed above the entry depth
//   Status_Unwind  => stack restored to the entry depth, unwind in flight
//
// Shape:
//
//   [[nodiscard]] Status build(State* vm, Ref src) {
//     OT_SCOPE(vm);
//     Ref tmp = ref_push(vm, ref_get(vm, src));
//     OT_CHECK(other(vm, tmp));        // on unwind, OT_SCOPE pops for us
//     Ref made = ref_top(vm);
//     OT_RETURN(ref_get(vm, made));
//   }
//
// OT_SCOPE arms a cleanup handler, so every path out of the region restores
// the stack without the function naming a pop. OT_RETURN disarms it, pops to
// the entry depth and pushes the single result; nothing allocates between that
// pop and that push, so the raw Value it carries cannot go stale.
//
// Ref, Status, ScopeGuard and OT_SCOPE itself are defined in slots.h; the
// pieces below are the raw-Value half, for files with direct heap access.

static inline Value ref_get(State* vm, Ref r) { return vec_at(&vm->stack, r.i); }
static inline void ref_set(State* vm, Ref r, Value v) { vec_at(&vm->stack, r.i) = v; }
static inline Ref ref_push(State* vm, Value v) { return (Ref){state_push(vm, v)}; }
// The cell a just-returned Status_Ok callee pushed its result into.
static inline Ref ref_top(State* vm) {
  OT_ASSERT(vm->stack.len > 0);
  return (Ref){vm->stack.len - 1};
}

// Disarm, drop the scope's temporaries, and leave `result` as the one value
// the Status_Ok contract promises. No allocation occurs between the pop and
// the push, so `result` cannot be moved out from under us in between.
static inline Status scope_return(ScopeGuard* g, Value result) {
  State* vm = g->vm;
  u32 base = g->base;
  g->vm = nullptr;
  if (vm->stack.len > base) vm->stack.len = base;
  state_push(vm, result);
  return Status_Ok;
}

// One OT_SCOPE per function (defined in slots.h). A second one shadows the
// first, which -Wshadow rejects under -Werror, so a region that wants its own
// scope becomes its own function. That is the intended shape: the scope and
// the function agree.
#define OT_RETURN(result) return scope_return(&_otScope, (result))

// Build a list from `n` values sitting in consecutive stack slots at `base`,
// folding right to left onto `tail`. The elements stay where they are and are
// read by index, so the make_pair chain moving them is harmless: their slots
// are roots. `tail` is rooted by the first push, before anything allocates.
// This fold was re-derived by hand in five places before it lived here.
static inline Value list_from_stack_onto(State* vm, u32 base, u32 n, Value tail) {
  OT_SCOPE(vm);
  Ref acc = ref_push(vm, tail);
  for (u32 i = n; i-- > 0;)
    ref_set(vm, acc, make_pair(vm, vec_at(&vm->stack, base + i), ref_get(vm, acc)));
  return ref_get(vm, acc);
}

static inline Value list_from_stack(State* vm, u32 base, u32 n) {
  return list_from_stack_onto(vm, base, n, null_v());
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
