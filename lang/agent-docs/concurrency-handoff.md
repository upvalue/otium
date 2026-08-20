# Concurrency handoff

The root process can yield and resume. Single-process programs, the REPL,
loaders, native calls, and interrupt behavior still use the existing synchronous
path.

Read `agent-docs/concurrency-proposal.md` first. It describes the larger
process model and the order after this milestone.

## Current state

Every registered special form compiles to printable ASCII bytecode. The VM does
not interpret or recompile an AST while executing. Dynamic-form descriptors
hold precompiled `OBJ_CODE` values.

`ots` now owns one embedded `root_process` and points at it through
`current_process`. Evaluation-owned state lives on `ot_process`:

- VM operand and frame arrays, counts, capacities, and frame limit.
- Current namespace and reduction accounting.
- Handler, restart, and dynamic-parameter stacks.
- Condition and unwind state.
- Interrupt and run state.
- Process-owned dynamic continuation records.

Runtime-wide heap, namespace, module, extension, loader, writer, and publisher
state remains on `ots`. Native functions still receive `ots*` and reach the
active process through `current_process`.

## Resumable execution

The public run boundary is:

```c
bool ot_start_call(ots* state, otv function, otv* args, size_t argc);
ot_run_result ot_run(ots* state, uint64_t reduction_budget);
```

`ot_start_call` seeds an interpreted function call in the root process.
`ot_run` returns `OT_RUN_COMPLETED`, `OT_RUN_YIELDED`, or
`OT_RUN_FAILED` with the returned or failed value. `OT_RUN_BLOCKED` is
reserved for scheduler and mailbox work. A zero budget runs without a reduction
limit.

`ot_eval_src` remains synchronous. This keeps the existing embedding API,
bootstrap, loader, REPL, and extension behavior unchanged.

Reduction checks happen at bytecode instruction boundaries. A nested synchronous
VM entry from a native function or interrupt hook cannot park because its C
caller is still live. Budget exhaustion there sets a pending yield; the outer
process-owned VM boundary yields as soon as the nested call returns. Native code
is still cooperative and can delay a scheduling slice until it returns.

## Dynamic continuations

These opcodes no longer keep their active extent in `vm_execute_dynamic` C
locals:

- `BC_TRY`
- `BC_HANDLER_BIND`
- `BC_RESTART_CASE`
- `BC_UNWIND_PROTECT`
- `BC_WITH_PARAMS`

Each opcode creates an `ot_vm_continuation` with an explicit phase, owner frame
and stack boundary, descriptor, environment, namespace, and opcode-specific
saved values. Handler, restart, and parameter frames that must survive a yield
are host-allocated with the continuation.

The continuation driver runs precompiled code until it returns, fails, creates a
nested dynamic continuation, or exhausts the budget. It then advances the
opcode state machine or propagates the unwind to the next continuation. No
source-form opcode or second evaluator was added.

The moving collector traces and validates:

- Suspended operand values and VM frames.
- Continuation descriptors, environments, namespaces, saved results,
  conditions, and restart arguments.
- Host-owned handler, restart, and parameter records.

The root process may be collected between any two yielded slices.

## Tests added

`tests/test_runtime.c` now starts root calls with small budgets and forces a
moving collection after every yield. It covers:

- CPU-bound tail calls returning the uninterrupted result.
- Normal completion and explicit failed results.
- `try` condition handling.
- Dynamic parameter bindings.
- Nested handlers, restart invocation, and unwind cleanup order.
- Interrupt unwinding through suspended cleanup code.
- Non-tail frame-limit failure after resumptions.

The dynamic tests use a budget of one instruction, so they park inside the
precompiled bodies rather than only around the dynamic opcode.

## Next work

Proceed in this order:

1. Add immutable PID and reference values.
2. Generalize process allocation and GC traversal beyond the embedded root.
3. Add a single-threaded round-robin runnable queue using `ot_run`.
4. Add `defvar` storage and distinct process-local lookup and assignment
   opcodes.
5. Add freezing and shareability checks.
6. Add graph copying for mutable messages.
7. Add bounded FIFO mailboxes and blocking send/receive.
8. Add exit reasons, monitors, links, supervision, inspection, and termination.

The compiler already distinguishes lexical and published names. Keep
process-local `defvar` reads and writes as a third opcode family. There is no
`unvar` operation.

The embedded root process is intentional for this milestone. Before adding
children, change GC process tracing from one direct `root_process` call to an
iteration over every live process, including blocked and exited processes whose
metadata is still retained.

## Constraints

- Keep platform-neutral VM and scheduler code in `src/otium.c`.
- Put clock, polling, wakeup, and other OS operations behind
  `src/ot-posix.c`.
- Preserve printable ASCII bytecode and four hexadecimal digits per operand.
- Do not add an AST evaluator or generic special-form escape opcode.
- Do not change the extension transfer API in the next pass.
- Do not port the roguelike or extension examples to processes yet.
- Ticket `lan-vboy` is independent computed-goto performance work.

## Verification

The current tree passes:

```text
make test
tools/format-c --check
for otium_test in tests/otium/*.scm; do
  OTIUM_GC_STRESS=1 build/otium --no-project "$otium_test" >/dev/null || exit
done
```

All 14 C runtime tests also pass with AddressSanitizer,
UndefinedBehaviorSanitizer, `OT_GC_VALIDATE`, and GC stress enabled.

The working tree contains a pre-existing deletion of `RUNTIME-REWRITE.md` and
a pre-existing edit to `prelude/expander.scm`. Preserve both. The bytecode and
root-process work modifies `src/main.c`, `src/otium.h`, `src/otium.c`,
`src/ot-gc.c`, and `tests/test_runtime.c`.
