// eval.hpp - top-level compilation, application, and VM control helpers.
#pragma once
#include "state.hpp"

namespace ot {

// FunctionData is defined in heap.hpp because the scavenger needs its layout.
inline FunctionData* fn_data(Value v) { return as_function(v); }
inline ParamData* param_data(Value v) { return as_param(v); }
inline RestartData* restart_data(Value v) { return as_restart(v); }

Value eval_form(State&, Value form);  // expand (via *expander*) + evaluate one top-level form
Value apply(State&, Value callee, u32 base, u32 argc);  // args on vm stack
Value start_quit(State&);                               // begin an uncatchable quit unwind

struct EvalSourceState {
  u32 consumed = 0;
  bool readError = false;
  bool incomplete = false;
};

struct EvalSourcePolicy {
  void* data = nullptr;
  Value (*eval)(State&, Value form, void* data) = nullptr;
  void (*afterEval)(State&, Value result, u32 consumed, void* data) = nullptr;
  EvalSourceState* state = nullptr;
};

// Read and evaluate forms in order, returning the last value (nil for an
// empty source) or the first read/evaluation unwind.
Value eval_source(State&, const char* src, u32 len, const char* name);
Value eval_source(State&, const char* src, u32 len, const char* name,
                  const EvalSourcePolicy& policy);

Value make_native(State&, const char* name, NativeFn);

// Compiler-only control primitives. The compiler stores these native
// functions directly in Code constant pools and passes compiled thunks for
// bodies that need a dynamic extent or may intercept an unwind.
Value vm_control_handler_bind(State&, u32 base, u32 argc);
Value vm_control_restart_case(State&, u32 base, u32 argc);
Value vm_control_try(State&, u32 base, u32 argc);
Value vm_control_unwind_protect(State&, u32 base, u32 argc);
Value vm_control_with_params(State&, u32 base, u32 argc);
Value vm_control_defparam(State&, u32 base, u32 argc);
Value vm_control_ns(State&, u32 base, u32 argc);
Value vm_control_in_ns(State&, u32 base, u32 argc);
Value vm_control_require(State&, u32 base, u32 argc);

// ---------------------------------------------------------------------------
// EXPANDER ORACLE API (for the self-hosted expander)
//
// Every top-level form is routed through the var `*expander*` in otium.core
// before evaluation: ((*expander*-value) form) -> expanded-form. The initial
// binding is a native that performs a simple recursive stage-0 expansion
// (macro calls in head position are expanded; `quote` is skipped). Rebind
// `*expander*` to install the self-hosted expander.
//
// Two oracle natives are defined in otium.core for it:
//
//   (expander-lexical? env-handle sym) -> boolean
//     env-handle is a lexical-scope handle the expander builds itself:
//     () for the empty scope, and (cons frame parent-handle) where `frame`
//     is any table whose KEYS are the symbols bound at that level (values
//     are ignored; use anything non-nil, e.g. #t). Returns #t iff `sym` is
//     a key in any frame of the chain. The empty handle () is what the
//     expander starts with at top level.
//
//   (expander-macro-var sym) -> macro | nil
//     Resolves `sym` as a var in the current namespace (own vars, refers,
//     and qualified p/n forms). Returns the macro object if the var's value
//     is a macro, else nil. Never raises.
// ---------------------------------------------------------------------------

}  // namespace ot
