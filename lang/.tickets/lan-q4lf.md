---
id: lan-q4lf
status: in_progress
deps: []
links: [lan-qwff]
created: 2026-08-16T01:28:30Z
type: task
priority: 1
assignee: Phil
---
# Bytecode compiler + stack VM, replacing the tree-walking evaluator

eval_tr is a tree-walker with no lexical addressing (env-chain walk + hash lookup per variable ref); performance is abysmal. Replace it with a compile-then-execute pipeline: single-pass bytecode compiler + stack-based interpreter. Decisions agreed with Phil: stack machine (not register), operands on the existing GC-rooted value stack, native (base, argc) ABI preserved so all ~110 builtins work unchanged; FULL replacement (prelude, self-hosted expander, REPL, runtime eval all compile; eval_tr deleted at the end); rename Vm -> State first (the struct is runtime state, not a VM), freeing the vm name for the actual machine. Full plan with instruction set, closure model, migration phases, and risk list in the Design section.

## Design

## Code objects
New ObjType::Code + Tag::Code (before Tag::Unwind; update is_heap upper bound). GC-heap header, bulk payload C-heap (finalizable, like Array/Table): pinned u8* bytes + Value* consts (contents GC-traced in place), nfixed/hasRest/nupvals/nlocals/maxStack/name. Pinning means the interpreter can hold raw ip/consts pointers across allocations; only header objects move.

## Closures
FunctionData becomes {name, code, nsName, native, docstring, nupvals + inline upvalue Values}; params/body/env dropped (only heap.cpp scavenger touches them outside eval.cpp). Flat closures, eagerly boxed captures: capture analysis per lambda; captured locals hold a make_box in their slot (GET/SET_BOXED), uncaptured locals are raw slots; upvalues always boxes copied by OP_CLOSURE. define-in-body is hoisted (slots nil-initialized at body entry -> internal mutual recursion works). define is a global ns_define iff no enclosing USER lambda (compiler-introduced thunks do not count).

## Instruction set (~30 ops)
CONST k / NIL TRUE FALSE NULL / INT8 / POP / POPN_KEEP1 n / GET,SET_LOCAL / GET,SET_BOXED / MAKE_BOX / GET,SET_UPVAL / GET_GLOBAL k (consts[k] = cached var cell, mutated in place on redefine -> malleability preserved; late-resolve if still a Symbol) / SET_GLOBAL / DEF_GLOBAL / CLOSURE / CALL argc (callee at top-argc-1, args contiguous = existing native convention) / TAILCALL / RETURN / JUMP JF JF_PEEK JT_PEEK / LOOP (backward jump + interrupt poll) / CONS / LIST n. Quasiquote lowers to CONS/LIST + %append2 (= list_append2).

Condition forms (handler-bind, restart-case, try/catch, unwind-protect, with-params) lower to thunk closures + hidden helper natives (%handler-bind etc.) stored directly as Function Values in the constant pool -- direct ports of the eval_tr logic; side-stack truncation (handlers/restarts/paramBindings on every exit path incl. Quit) stays inside the helpers. Same for %require/%in-ns/%defparam.

## Interpreter
CallFrame {Value fn; const u8* ip; Value* consts; u32 base; u32 savedNs}; Vec<CallFrame> frames on State, fn traced by vm_walk_roots. vm_execute(State&, u32 floor), switch dispatch. Never cache CodeData*/FunctionData* across an alloc -- re-derive from frames.last().fn. TAILCALL shifts callee+args to base, reuses frame (keeps savedNs) -> constant-space loops per spec 3.5. RETURN restores currentNs. Unwind: callee returns Tag::Unwind -> pop frames to floor, propagate. Re-entrancy: apply pushes a frame and calls vm_execute(state, frames.len-1) for compiled callees (expander, map, eval, require all work). Top-level thunk executes without ns restore so ns/in-ns persist. Depth guard = frame count vs cfg.maxDepth; maxStack checked at CALL.

## Compiler
src/compile.cpp: single-pass emit over expanded forms + per-lambda pre-pass (hoisted defines, capture analysis). Constants accumulate in a GC-rooted Array via Slot; compiler walks heap pairs while allocating so every cursor obeys Slot/Scope discipline (GC-stress build is the gate). Scope rules mirror prelude/expander.scm exactly; tail positions per spec 3.5. Globals resolved via ns_resolve_var at compile time, var cell cached in pool, Symbol stored for late binding on miss. Small disassembler in code.hpp. Bootstrap: *expander* starts as native expand0; boot compiles+runs expander.scm (special forms only), which rebinds *expander*; prelude follows -- same staging as today.

## Phases (each commit green on doctests + tests/otium/run-tests.py)
A. Rename Vm->State (vm.{hpp,cpp} -> state.{hpp,cpp}), mechanical standalone commit; keep heap-first-member invariant.
B. Code object + machine skeleton + test_vm.cpp on hand-assembled bytecode (incl. GC-stress alloc loop, unwind-through-frames).
C. Compiler core subset (literals..quasiquote) behind OT_USE_VM flag; conformance 01-07 through BOTH paths; default stays tree-walking.
D. Full form set (ns/require/conditions/params/defmacro); full suite green both ways.
E. Flip default; bootstrap through VM; GC-stress EVERY=1 + sanitize + REPL smoke; record benchmark before/after.
F. Delete eval_tr and dead helpers, drop FunctionData params/body/env, split surviving natives out of eval.cpp (spirit of lan-xix2). Survivors: apply (non-compiled branches), signal_value, raise_error, param_read, list_from_stack, make_box, expand0 + oracle natives, require_load/require_spec, list_append2.

## Risks
(1) moving GC: re-derive header pointers after allocs; (2) var-cell caching freezes which var a name denotes (Clojure-like) -- audit conformance 06/10; (3) hoisted define: read-before-define -> nil, not global fallback -- verify untested; (4) top-level define inside lowered thunks must still ns_define; (5) Quit must run unwind-protect and bypass try/handler-bind -- add doctests; (6) check Tag::Unwind after every native call in the loop; (7) savedNs across TAILCALL; (8) bootstrap bugs pre-flip appear as fatals -- keep disassembler + parallel flag until F.

## Acceptance Criteria

- Vm renamed to State in a standalone commit; heap remains first member.
- Full conformance suite (tests/otium) and all doctest binaries pass with the VM as the only evaluator; eval_tr and the env-chain machinery are deleted.
- GC-stress build (OT_GC_STRESS_EVERY=1) and sanitize build pass the full suite.
- Proper tail calls verified constant-space (tail-recursive counter to ~3e6 without overflow); non-tail depth overflow is a catchable error.
- Conditions/restarts/unwind-protect/with-params semantics unchanged, incl. handlers running at the signal site and Quit behavior; REPL restart chooser still works; Ctrl-C interrupts (while #t nil).
- Live redefinition still visible to previously compiled code (var cells mutated in place).
- Benchmarks recorded before/after: fib.scm plus new loop.scm and tables.scm.

