# Concurrency handoff

Otium can now run basic concurrent programs. The runtime has multiple
lightweight processes, preemptive round-robin bytecode scheduling, opaque
process identities, bounded FIFO mailboxes, blocking receive, and copied
mutable messages.

Read `agent-docs/concurrency-proposal.md` for the intended larger model. This
document describes the implemented boundary and the remaining gaps.

## Implemented process model

`ots` retains an embedded root process and owns a linked list of every live
process. Each process owns its VM frames and operand stack, dynamic
continuations, condition and unwind state, namespace cursor, reduction
accounting, PID, scheduler state, and mailbox.

The collector traces and validates every live process, including blocked
processes. It updates process identities, VM state, dynamic extents, suspended
native-call results, and mailbox values during a moving collection.

The scheduler is single-threaded and round-robin. A configurable bytecode
reduction budget preempts CPU-bound Otium code; explicit `yield` ends a slice
early. Native code remains cooperative until it returns. Successful source and
REPL evaluation drain the runnable queue until every child has exited or is
blocked.

The existing embedding boundary remains available:

```c
bool ot_start_call(ots* state, otv function, otv* args, size_t argc);
ot_run_result ot_run(ots* state, uint64_t reduction_budget);
```

`ot_config` also has `reductions_per_slice` and `mailbox_count`. Their defaults
are 1024 bytecode reductions and 32 messages.

## Language surface

The initial process API is:

```lisp
(spawn function argument ...)
(self)
(pid? value)
(alive? pid)
(make-ref)
(ref? value)
(yield)
(send pid value)
(send! pid value)
(receive)
```

`spawn` accepts an interpreted function plus arguments. It returns immediately
with the child PID; the child is placed on the runnable queue. The child exits
when the function returns or an unhandled condition escapes it.

`send` returns one of `:ok`, `:full`, `:dead`, or `:not-sendable`. `send!`
returns the original value on success and raises a condition for the other
outcomes. A receive from an empty mailbox suspends the process. Sending to that
process installs the result at the suspended bytecode call site and makes the
process runnable. This works for ordinary and tail-position calls.

PIDs contain an ID and generation and print as `#<pid ID.GENERATION>`. Keeping
a PID does not keep an exited process alive. References are unique opaque
values and print as `#<ref ID>`.

## Message isolation

Immediate values, numbers, names, strings, PIDs, references, native functions,
and interpreted functions without captured lexical state are shared as
immutable values. Mutable pair and array graphs are copied. Copying preserves
cycles and internal sharing, and the sender retains its original graph.

Tables, buffers, extension values, active runtime objects, and interpreted
closures with captured lexical environments are currently rejected. The
rejection is atomic: no partial message is left in the mailbox.

Spawn arguments cross the same copying boundary. Until closure-environment
copying exists, spawn a named function and pass its state as explicit
arguments.

## Examples and tests

Run the two demos with:

```text
build/otium --no-project examples/concurrency-basic.scm
build/otium --no-project examples/concurrency-mailbox.scm
```

The first shows stable round-robin interleaving and distinct PIDs. The second
shows blocking receive, wakeup, FIFO delivery, and copied array/list messages.

`tests/otium/11-concurrency.scm` covers the language surface and deterministic
scheduling. The C runtime concurrency test runs with collection on every
allocation, a one-message mailbox, and three-reduction slices. It covers full
and dead mailboxes, blocked wakeup, ordinary and tail receive, unsupported
values, copied arrays, cyclic graphs, isolated child failure, and result rooting
while the scheduler runs.

## Known gaps

This is the basic-demo milestone, not the complete proposal:

- Published namespace Vars are still runtime-wide. `defvar` and distinct
  process-local bytecode operations are the next isolation-critical feature.
- Tables, buffers, closure environments, and extension values are not copied.
  There is no public transitive `freeze` operation yet.
- Exit metadata is discarded when a child exits. There are no monitors, links,
  supervision messages, cancellation, or process inspection.
- Receive is FIFO only; it has no pattern selection or timeout.
- Child failures are isolated but currently silent.
- Scheduling happens at evaluation boundaries. There is no public embedding API
  for independently pumping the child scheduler yet.
- The heap is still runtime-wide physically. The implemented transfer boundary
  prevents ordinary pair/array aliasing, but per-process heaps remain future
  work.

## Next work

Proceed in this order:

1. Add `defvar` storage plus explicit process-local load and assignment
   bytecodes, and prohibit mutation of published definitions from processes.
2. Add transitive freezing and extend the graph copier to tables, buffers, and
   closure environments.
3. Retain exit reasons and add monitors before links and supervision.
4. Add scheduler inspection, cancellation, receive timeouts, and an embedding
   scheduler-pump API.
5. Move toward per-process heaps and only then M:N workers.

Keep platform-neutral VM and scheduler code in `src/otium.c`. Preserve printable
ASCII bytecode and do not reintroduce an AST evaluator escape path. The
computed-goto ticket `lan-vboy` and resume-record rename `lan-47ft` are
independent work.

The working tree contains a pre-existing deletion of `RUNTIME-REWRITE.md`, a
pre-existing edit to `prelude/expander.scm`, and pre-existing untracked ticket
files. Preserve them when committing this milestone.
