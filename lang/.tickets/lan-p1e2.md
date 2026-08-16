---
id: lan-p1e2
status: open
deps: []
links: [lan-70gb, lan-67q1]
created: 2026-08-16T04:26:30Z
type: epic
priority: 2
assignee: Phil
tags: [performance, vm, jit, benchmarks]
---
# LLVM-backed native code generation for hot functions

Strawman spec for discussion — nothing here is decided. The goal is to move
the r7rs benchmark numbers (lan-70gb) from "two orders of magnitude off" to
"within small-integer factors of mature Schemes", using LLVM as the backend,
without compromising the interpreter as the canonical, low-memory product.

## Where the time actually goes

Current numbers: fib:40 = 86s, tak = 44s, cpstak/takl/nqueens time out at
120s. Chez runs fib:40 in ~2s. The gap is not primarily bytecode dispatch:

1. **Every arithmetic/comparison op is a full call.** There are no arith
   opcodes; `(+ a b)` compiles to `GetGlobal` (var-cell load via the constant
   pool), `Call 2`, which lands in `apply`, which invokes the native builtin
   through a function pointer with `(State&, base, argc)`. That is easily
   20-50x the cost of an add-with-guard.
2. **Dispatch + frame overhead** in `vm_execute` (already mitigated by
   computed goto, but still a load/branch per instruction).
3. Everything flows through the shadow stack (`state.stack`) with bounds and
   sanity checks (`VM_NEED_*`) on every instruction, even though `code_verify`
   already proved most of them.

Consequence: **LLVM applied naively to today's bytecode would mostly inline
dispatch and win maybe 2-3x.** fib would still spend its life inside `apply`.
The spec therefore has a non-LLVM prerequisite phase that is worth doing even
if we never merge the LLVM work.

## Phase 0 (prerequisite, no LLVM): intrinsic opcodes

Add guarded fast-path opcodes for the hot builtins:

    Add, Sub, Mul, Div, NumLt, NumLe, NumGt, NumGe, NumEq,
    Car, Cdr, NullP, PairP, Not

Malleability constraint: globals are redefinable and var cells are mutated in
place, so the compiler cannot burn `+` into an add unconditionally. Strawman
guard, preserving full redefinition semantics at ~2 loads + 1 compare:

- The compiler emits `Add <u16 constIndex>` when the callee is a symbol that
  resolves (at compile time, via the normal `GetGlobal` resolution path) to
  the core builtin. `constIndex` points at the same var-cell constant a
  `GetGlobal` would use.
- At runtime the op loads the var cell, checks `var_value(var)` still holds
  the original native (compare `FunctionData::native` pointer), and checks
  both operands are `Tag::Int`. If all hold: inline add (with overflow check
  matching the builtin's behavior). Otherwise: fall through to the generic
  `apply` path with identical semantics, including int/float promotion and
  error raising.
- Quasiquote's existing `Cons/List/Append2` stay as-is (they're tied to
  syntax, not bindings, so they need no guard).

This alone should be a large multiple on fib/tak/takl-shaped code and speeds
up the interpreter for every user, including the low-memory targets that will
never link LLVM. It also creates the IR-level seams the JIT needs: the JIT
compiles the *same* guarded ops to native compare-and-branch, and the guards
become branch-predicted no-ops.

Verifier: `code_verify` learns the new ops (operand `U16`, stack effect
2→1 or 1→1) so the JIT can later trust the invariants.

## Phase 1: method JIT via LLVM ORC (LLJIT)

Unit of compilation: one `Code` object → one native function. Nested
`Closure` descriptors compile independently (on their own hotness).

### Execution model

- Native signature: `Value ot_jit_entry(State&)` operating on the *same*
  frame the interpreter would use: `enter_frame` has already run, locals and
  operand stack live in `state.stack`. `CodeData` grows a `void* jitEntry`
  (C-heap pointer; the Code object moves under GC but the field moves with
  it, and JITted code is never on the GC heap).
- `vm_execute`'s `Call`/`TailCall`/`vm_call` check `jitEntry` after
  `enter_frame` and invoke it instead of the bytecode loop. JITted code
  makes outgoing calls through a small helper (`vm_call_from_jit`) that
  dispatches to callee's `jitEntry`, the interpreter, or `apply` — so mixed
  interpreted/compiled execution works in both directions from day one.
- Returns `Value`; `Tag::Unwind` propagates exactly as today (JITted code
  checks the tag after every call, same as `OT_TRY`).

### Tiering

v0: `--jit` flag (and `OT_JIT=1`) compiles every verified `Code` eagerly at
`make_code` time — simplest to test, fine for benchmarks. v1: per-code hot
counter (calls + `Loop` back-edges) with a threshold, compile on trip, so
the REPL never pays compile latency for one-shot code.

### Correctness constraints (the load-bearing list)

- **Moving GC.** The Cheney scavenger moves everything. Rules for generated
  code: Values live in `state.stack` slots (already roots) at every possible
  collection point; SSA registers may cache Values only between safepoints
  (any allocation, any call); raw `Obj*`/`CodeData*`/derived pointers never
  survive a safepoint. Also `state.stack.data` itself can move (Vec
  realloc on push) — reload the base pointer after anything that can push.
  v0 can be maximally conservative (every op reads/writes the shadow stack,
  exactly mirroring the interpreter); the win still comes from erased
  dispatch, erased `VM_NEED` checks (verifier proved them), and inlined
  Phase-0 guards.
- **Globals.** Var cells are GC-heap Arrays mutated in place; the interpreter
  caches resolution in the traced constant pool (`global_cell`). JITted code
  must do the same loads through `consts` — it must not bake a cell address
  into code. (If this ever shows up in profiles, the fix is pinning var
  cells, which is a separate design discussion.)
- **Tail calls.** Self-tail-call (callee var still resolves to the function
  being compiled, guarded like Phase 0) becomes argument shuffling + a branch
  to the entry block — this is what rescues cpstak/takl. General tail calls
  cannot grow the C stack: JITted code returns a `TailCallRequest` sentinel
  to a trampoline in `vm_call_from_jit` rather than calling the callee
  directly. Non-tail JIT→JIT calls use real C-stack recursion, bounded by
  the existing `maxDepth` check in `enter_frame`.
- **Interrupts.** Poll `state.interruptFlag` at `Loop` back-edges and after
  calls, matching `VM_POLL_INTERRUPT` placement, so Ctrl-C latency is
  unchanged.
- **Redefinition mid-run.** A function redefined while executing keeps
  running its old code (same as interpreter — frames hold the `Function`
  value). New calls resolve through var cells and pick up new code. No
  invalidation machinery needed because nothing about a *binding* is baked
  into code; only Phase-0 guards reference specific natives and they
  fall back dynamically.

### What the JIT does NOT do (v0)

No unboxing across ops (Values stay 16-byte tagged), no escape analysis, no
inline caches for user functions, no OSR (a loop already running in the
interpreter finishes there; the *next* call runs native). Each is a possible
Phase 2 line item, gated on profiles, not speculation.

## Phase 2 (sketch, separate tickets when Phase 1 lands)

- Stack→SSA modeling between safepoints so LLVM can keep hot temporaries in
  registers (biggest expected win after Phase 1).
- Int-typed loop specialization: guard on entry, run an unboxed i64 loop
  body, deopt to the generic path on guard failure.
- fib-shaped self-recursion: direct native call to own entry, skipping
  `vm_call_from_jit`.

## Build integration

- Meson: `option('llvm', type: 'feature', value: 'disabled')`;
  `dependency('llvm', method: 'config-tool', modules: ['orcjit', 'native'])`.
  Guard all JIT sources behind `OT_LLVM_JIT`; they live in `src/jit/` and are
  the only files allowed to include LLVM headers.
- `libotium` stays LLVM-free. The JIT links into the `otium` executable
  build only. `-fno-exceptions/-fno-rtti` is compatible with LLVM's C++ API
  (LLVM itself builds that way).
- This is a benchmark/desktop feature. The low-memory story is the
  interpreter; LLJIT costs tens of MB resident and O(10ms+) per function
  compile, and that is accepted and documented, not fought.

## Testing / acceptance

- Full test suite passes with `--jit` forced on (CI job alongside the
  interpreter job, when LLVM is available).
- Differential harness: run every test file and every r7rs port under both
  engines, compare results and raised conditions.
- `run.py` grows an engine column so STATUS.md shows interp vs jit
  side by side.
- Rough targets (guesses, to be revisited after Phase 0 lands):
  Phase 0 alone: fib:40 under ~15s, no timeouts in the 12-benchmark set.
  Phase 1: fib:40 under ~5s. Phase 2: single digits everywhere Chez is.

## Alternatives considered

- **Template/copy-and-patch JIT** (femtolisp-adjacent, or Python 3.13
  style): tiny footprint, no dependency, ~interpreter×3-5. Better fit for
  the low-memory ethos, worse ceiling, more per-arch work. Real contender —
  worth discussing before committing to LLVM.
- **AOT: emit C (or LLVM IR) offline** and link: no runtime dependency, but
  wrong shape for a malleable image-based language; punts on eval/REPL.
- **Cranelift-style baseline via custom emitter**: LLVM-free but a large
  bespoke surface per architecture.

LLVM is proposed because it has the highest ceiling, the module boundary
(`src/jit/`, optional dependency, same execution model) is identical for any
backend — so a later swap to copy-and-patch reuses everything but the
emitter.

## Open questions for discussion

1. Is a hard LLVM dependency (even optional) acceptable at all, or should
   Phase 1 target copy-and-patch and keep LLVM as a possible tier 3?
2. Phase 0 guard strategy: is compare-against-original-native the right
   redefinition semantics, or should intrinsics only fire for a
   `declare`-style opt-in?
3. Overflow semantics for inline `Add`/`Mul` fast paths — what does the
   builtin do today, and is that contract stable enough to duplicate?
4. Eager-compile flag vs hotness tiering as the v0 milestone?
