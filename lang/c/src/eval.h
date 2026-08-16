// eval.h - top-level compilation, application, and VM control helpers.
#pragma once
#include "state.h"

// FunctionData is defined in heap.h because the scavenger needs its layout.
static inline FunctionData* fn_data(Value v) { return as_function(v); }
static inline ParamData* param_data(Value v) { return as_param(v); }

Value eval_form(State* vm, Value form);  // expand (via *expander*) + evaluate one top-level form
Value apply(State* vm, Value callee, u32 base, u32 argc);  // args on vm stack
Value start_quit(State* vm);                               // begin an uncatchable quit unwind

typedef struct EvalSourceState {  // zero-init
  u32 consumed;
  bool readError;
  bool incomplete;
} EvalSourceState;

typedef struct EvalSourcePolicy {  // zero-init
  void* data;
  Value (*eval)(State* vm, Value form, void* data);
  void (*afterEval)(State* vm, Value result, u32 consumed, void* data);
  EvalSourceState* state;
} EvalSourcePolicy;

// Read and evaluate forms in order, returning the last value (nil for an
// empty source) or the first read/evaluation unwind.
Value eval_source(State* vm, const char* src, u32 len, const char* name);
Value eval_source_policy(State* vm, const char* src, u32 len, const char* name,
                         const EvalSourcePolicy* policy);

Value make_native(State* vm, const char* name, NativeFn fn);

// Compiler-only control primitives. The compiler stores these native
// functions directly in Code constant pools and passes compiled thunks for
// bodies that need a dynamic extent or may intercept an unwind.
Value vm_control_handler_bind(State* vm, u32 base, u32 argc);
Value vm_control_restart_case(State* vm, u32 base, u32 argc);
Value vm_control_try(State* vm, u32 base, u32 argc);
Value vm_control_unwind_protect(State* vm, u32 base, u32 argc);
Value vm_control_with_params(State* vm, u32 base, u32 argc);
Value vm_control_defparam(State* vm, u32 base, u32 argc);
Value vm_control_ns(State* vm, u32 base, u32 argc);
Value vm_control_in_ns(State* vm, u32 base, u32 argc);
Value vm_control_require(State* vm, u32 base, u32 argc);

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
