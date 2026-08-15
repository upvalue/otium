// vm.hpp — the Vm: heap, intern table, value stack (GC root), namespace
// registry, unwind state, handler/restart/param stacks.
#pragma once
#include "common.hpp"
#include "vec.hpp"
#include "value.hpp"
#include "heap.hpp"
#include "intern.hpp"

namespace ot {

struct VmConfig { u32 heapBytes; u32 stackSlots; u32 maxDepth; };

using WriteFn = void (*)(void* ud, const char* s, u32 n);
using LoadFn  = bool (*)(void* ud, const char* nsName, Buf* srcOut);

// What kind of unwind is in flight when a Tag::Unwind value propagates.
enum class UnwindKind : u8 { None, Condition, Restart, Quit };

struct HandlerBinding { Value pred, handler; };   // rooted by handler-bind's stack slots
struct RestartRec     { Value restart; };         // Tag::Restart value, rooted by restart-case
struct ParamBinding   { Value param, value; };    // rooted by with-params

// Pre-interned symbol/keyword name ids.
struct Syms {
  u32 quote_, if_, define_, def_, definePriv_, setBang_, lambda_, fn_,
      defmacro_, begin_, do_, let_, while_, and_, or_, cond_, else_,
      quasiquote_, unquote_, unquoteSplicing_, ns_, inNs_, require_,
      handlerBind_, restartCase_, try_, catch_, unwindProtect_, defer_,
      defparam_, withParams_, array_, table_, amp_,
      otiumCore_, user_, expander_, error_, quit_;
  // keyword name ids
  u32 kwType, kwMessage, kwData, kwName, kwVars, kwAliases, kwRefers,
      kwOrder, kwAs, kwRefer, kwReload, kwRequire;
};

struct Vm {
  Heap heap;
  Intern intern;
  Vec<Value> stack;                 // GC root: the value stack
  VmConfig cfg;

  Value nsRegistry;                 // table nsName -> ns record (also in stack[0])
  Value typeParents;                // condition type registry (also in stack[1])
  u32 currentNs;                    // intern id of the current namespace name
  u32 expandNs;                     // ns of the form being expanded (0 = none);
                                    // the macro oracle resolves against this,
                                    // since applying the expander closure
                                    // switches currentNs to its defining ns
  u64 gensymCounter;
  u64 restartIdCounter;
  u32 depth;                        // non-tail eval nesting

  volatile bool interruptFlag;
  WriteFn writeFn; void* writeUd;
  LoadFn loadFn;  void* loadUd;

  // Unwind state, valid while a Tag::Unwind value is propagating.
  // NOTE for the heap agent: unwindCondition and unwindRestartArgs (and the
  // handlers/restarts/paramBindings vectors) must be traced as GC roots in
  // addition to `stack` and the namespace registry.
  UnwindKind unwindKind;
  Value unwindCondition;
  u64   unwindRestartId;            // target restart when unwindKind==Restart
  Value unwindRestartArgs;          // list of args for the restart clause

  Vec<HandlerBinding> handlers;     // innermost last
  u32 handlerVisible;               // handlers[i] visible to signal iff i < this
  Vec<RestartRec> restarts;         // innermost last
  Vec<ParamBinding> paramBindings;  // innermost last
  Vec<u32> loadingNs;               // require cycle detection

  Syms syms;

  static Vm* create(const VmConfig&);
  void destroy();

  // native calling convention: args at stack[base..base+argc)
  u32 push(Value v) { stack.push(v); return stack.len - 1; }
  void popTo(u32 base) { stack.len = base; }

  explicit Vm(const VmConfig&);
};

using NativeFn = Value (*)(Vm& vm, u32 base, u32 argc);

// Build {:type 'error :message <formatted>}, signal it through active
// handlers, and (if all decline) start a condition unwind. Returns Unwind.
Value raise_error(Vm&, const char* fmt, ...);

// Signal-site walk (spec 8.2). Handlers run innermost-out with the stack
// intact; a handler and everything inner to it is invisible while it runs.
// If all decline: starts an unwind when unwindIfUnhandled, else returns nil.
Value signal_value(Vm&, Value condition, bool unwindIfUnhandled);

// Host-side handler installation (the REPL's interactive restart chooser).
// pred and handler are callable Values; the binding is GC-rooted by the
// handlers vector itself. Push/pop must nest.
Value vm_push_handler(Vm&, Value pred, Value handler);
void  vm_pop_handler(Vm&);

// Sanctioned cancel for an in-flight unwind (e.g. macroexpand probing a
// head symbol that doesn't resolve). Clears all unwind state.
void vm_cancel_unwind(Vm&);

} // namespace ot
