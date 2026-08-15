// eval.hpp — stage-0 evaluator: trampoline with TCO, special forms,
// conditions/restarts, dynamic params.
#pragma once
#include "vm.hpp"

namespace ot {

// FunctionData is defined in heap.hpp (the scavenger needs its layout).
// Local aliases for the payload accessors:
inline FunctionData* fn_data(Value v) { return as_function(v); }
inline ParamData* param_data(Value v) { return as_param(v); }
inline RestartData* restart_data(Value v) { return as_restart(v); }

// Lexical environments are chains: () is the empty env; a non-empty env is
// (pair frame parent-env) where `frame` is a table mapping name-symbol -> a
// one-element array "box" whose slot 0 holds the binding's value (boxing
// keeps nil storable and gives set! a mutable cell).

Value eval_form(Vm&, Value form);           // expand (via *expander*) + evaluate one top-level form
Value eval_in(Vm&, Value form, Value env);  // evaluate with lexical env; restores current ns
Value apply(Vm&, Value callee, u32 base, u32 argc);  // args on vm stack

Value make_native(Vm&, const char* name, NativeFn);
void register_eval_natives(
    Vm&);  // defines signal/error/restart/condition/oracle natives into otium.core

// ---------------------------------------------------------------------------
// EXPANDER ORACLE API (for the prelude / self-hosted expander agent)
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
