# Processes and concurrency in Otium

**Status:** process model proposal. The bytecode prerequisite and resumable root
process are implemented. PIDs, scheduling, process-local Vars, copying, and
mailboxes are not implemented.

Otium should use isolated processes with FIFO mailboxes. A process executes one
thing at a time, owns its mutable state, and communicates by sending values.
Sending a mutable value copies it. Immutable code and data may be shared.

The initial runtime should run every process on one OS thread. This is enough to
settle the language model, make blocking and failure observable, and support an
Otium task manager. It also avoids tying the language to a collector design.
M:N scheduling and a different collector can come
later without changing Otium programs.

Concurrency should not make ordinary programs unpleasant. A program that never
calls `spawn` still runs in a root process, and local mutation works as it does
today. The isolation boundary becomes visible when a value is published or
crosses from one process to another.

## Implementation handoff

The bytecode parity work and first process milestone are in the current working
tree. One root process can exhaust a reduction budget, return to its caller,
survive a moving collection, and resume.

The implementation follows the existing file layout. The compiler, VM, and
dynamic-form opcode helpers are in `src/otium.c`; object and state layouts are in
`src/otium.h`; VM roots are traced and validated in `src/ot-gc.c`. No VM or
process source file was added. `src/ot-posix.c` was not changed.

### What is done

`OBJ_CODE` holds bytecode bytes, a constant pool, parameters, and a name.
`OBJ_FUNCTION` now holds code, its captured environment, defining namespace,
and name. It no longer has parameter or AST-body fields. Executable source forms
are not carried into the VM. Nested functions and dynamic-form bodies are
compiled to `OBJ_CODE` constants before execution.

Bytecode is a printable ASCII string, and every operand is four ASCII
hexadecimal digits. The instruction set covers constants, lexical and published
loads, calls and tail calls, returns, jumps, lexical scope entry/binding/exit,
closures, definitions, separate lexical and published assignments, list
construction for quasiquote, namespace operations, and dynamic forms.
`ot_function_bytecode` exposes the byte span for tests and future serialization
work.

The compiler directly lowers applications and every registered special form:

- `quote`, `begin`, `do`, `if`, `and`, `or`, `cond`, `while`, and `set!`.
- `lambda`/`fn`, definitions, macros, parameters, ordinary `let`, and named
  `let`.
- `quasiquote`, including unquote and splicing list construction.
- `try`, `handler-bind`, `restart-case`, `unwind-protect`/`defer`, and
  `with-params`.
- `in-ns`, `ns`, and `require`.

The old `BC_SPECIAL` and `BC_TAIL_SPECIAL` evaluator escape path has been
removed. The VM never recompiles an AST or calls a form evaluator while running
bytecode. Dynamic-form opcodes refer to descriptors containing precompiled code
objects. `try`, `handler-bind`, `restart-case`, `unwind-protect`, and
`with-params` now resume through process-owned continuation records instead of
keeping their active extent in C locals.

Bytecode calls run on host-managed frame and operand arrays in `ot_process`.
The collector updates values in both arrays and every dynamic continuation
during a moving collection. A tail call reuses its current VM frame; a non-tail
call consumes one frame and is limited by the process frame limit initialized
from `ot_config.max_depth`. Functions resolve published names in their defining
namespace, but calls do not change the process's current namespace. The latter
matters to macro expansion: the core expander must continue looking up new
macros in the caller's current namespace.

`ot_state` embeds one root `ot_process` and exposes it internally through
`current_process`. `ot_start_call` seeds an interpreted root function and
`ot_run` executes it with a bytecode reduction budget. The result reports
completed, yielded, blocked, or failed status plus a value. Blocked is reserved
for later mailbox work. Existing `ot_eval_src` evaluation remains synchronous.

Nested synchronous VM entry from native functions and interrupt hooks defers a
pending yield until it returns to the process-owned boundary. Native code can
therefore delay a slice, but the runtime never parks while a required native C
continuation is live.

The compiler already distinguishes lexical and published reads and writes. It
does not have process-local lookup or assignment because `defvar` does not
exist yet. Lexical values are still stored in linked heap environments rather
than indexed VM slots.

### Verification baseline

The following passed after the VM change:

```text
make test
tools/format-c --check
```

`make test` covers 14 C runtime tests, 10 language conformance groups, the CLI,
and the demo, OTCL, and Raylib extension tests. The runtime suite also passed an
AddressSanitizer and UndefinedBehaviorSanitizer build with `OT_GC_VALIDATE` and
GC stress enabled.
All 10 language conformance files also passed individually with
`OTIUM_GC_STRESS=1`.
Tests assert printable bytecode, distinct lexical/published load and assignment
opcodes, dedicated dynamic-form bytecode with no legacy evaluator opcodes, a
50,000-call tail loop, moving-GC roots, and VM frame-limit unwinding. The root
process tests force collection after every yield and cover conditions,
restarts, cleanup, parameters, interrupts, tail calls, and frame-limit failure.

The last `make bench` run on the development machine reported:

| benchmark | median |
|---|---:|
| `fib` | 3.582 s |
| `loop` | 349.73 ms |
| `tables` | 37.22 ms |

These numbers include process startup and prelude loading. `fib` is much slower
than the older C++ VM result recorded in `benchmarks/README.md`. The current VM
allocates linked lexical environments on calls and resolves locals by name;
indexed frame slots and captured upvalues are the likely next performance work.
That optimization is not required to start the process model.

Performance ticket `lan-vboy` tracks a computed-goto dispatch loop for
GCC/Clang with a portable fallback. It should be treated as a modest dispatch
improvement, not a substitute for fixing lexical access and call allocation.

### Boundary for the next agent

The next milestone starts with immutable PID and reference values, then adds
more process records and a round-robin scheduler. The root process is embedded
in `ot_state`; generalize GC traversal to visit every live process before a
child can be suspended or blocked.

Use `ot_run` as the scheduler slice boundary. A zero budget means unlimited
execution, and budget exhaustion returns `OT_RUN_YIELDED` without creating a
condition. `OT_RUN_BLOCKED` exists for receive and timer work but is not emitted
yet.

After PID/ref and scheduling, add `defvar` opcodes and storage,
freezing/shareability, graph copying, and bounded mailboxes. Do not change the
extension transfer API or port the roguelike and extension examples in this
pass.

The working tree also contains a pre-existing deletion of
`RUNTIME-REWRITE.md` and a pre-existing edit to `prelude/expander.scm`. They are
not part of the VM work and should not be restored or rewritten during the
handoff.

## Goals

The model should provide the following:

- Sequential execution within a process.
- No mutable state shared between ordinary Otium processes.
- FIFO mailboxes with explicit memory bounds.
- Cheap processes that do not each require an OS stack or thread.
- Fair scheduling of CPU-bound Otium code, even in the single-threaded runtime.
- Process failure that does not take down unrelated processes.
- Monitoring, supervision, process inspection, and hard termination.
- Live definition replacement and small, explicit edits to process-local state.
- A useful single-process programming model with ordinary mutable variables and
  containers.

The initial proposal does not include structural pattern matching, selective
receive, shared atomics, locks, channels, distributed processes, an M:N
scheduler, a GC algorithm change, or a full debugger.

## The process model

Every evaluation happens in a process. The runtime creates a root process before
it evaluates the program or starts the REPL. Programs that do not use the
process API behave like single-process Otium programs.

A process contains:

- A PID.
- A bytecode continuation: frames, operand stack, instruction pointer, and
  current namespace.
- Lexical environments and process-local namespace bindings.
- Dynamic parameter, condition handler, and restart stacks.
- A bounded user mailbox and a small runtime control queue.
- Links and monitors once those are established.
- Scheduling state and accounting information.

Only one worker executes a process at a time. A later M:N runtime may run
different processes in parallel, but it must never run the same process on two
workers simultaneously.

The thunk passed to `spawn` takes no arguments. The new process exits
normally when that function returns. An unhandled condition terminates only that
process. The runtime records the exit reason and notifies its monitors.

PIDs are immutable, opaque values. A PID contains enough generation information
that a dead PID can never begin naming a newly created process. Keeping a PID
alive does not keep the process alive.

## Isolation and ownership

The language invariant is:

> A mutable object may be reachable from the roots of at most one process.
> Shared runtime roots may reach only transitively immutable values.

This is semantic ownership. It does not require an owner field in every heap
object. The runtime preserves the invariant at the few operations that can
introduce a reference into another root set:

- Allocation gives a mutable object to the currently executing process.
- `spawn` copies mutable values captured by the child closure.
- `send` copies the mutable portion of a message into the recipient.
- `receive` gives the receiver a value already owned by it.
- Definition publication rejects values that are not shareable.
- Native extensions must declare whether their values are shareable, copyable,
  or confined to one process.

A debug build may tag objects with an owner PID or maintain an ownership side
table. That would be useful for catching runtime and extension bugs. Release
builds should not need that per-object cost.

### Shareable values

A value is *shareable* when it and everything reachable through it is
immutable. The following are shareable:

- Immediate values, symbols, keywords, and strings.
- PIDs and references.
- Frozen pairs, arrays, and tables whose contents are shareable.
- Bytecode and native functions with shareable captured environments.
- Published namespace Vars. Only the code publisher may change the value in
  one of these Vars.
- Extension values whose type declares that they are shareable.

Active restarts are process-local and are not sendable. A closure with mutable
captures is sendable, but not shareable: its environment is copied for the
recipient. A process-affine extension value is neither shareable nor copyable
and makes an attempted send fail.

Otium will need an explicit operation such as `(freeze value)`. It first checks
the entire reachable graph, then transitively marks pairs, arrays, and tables as
immutable. The operation either succeeds completely or leaves the graph
unchanged. Mutation of a frozen object is an error. Buffers remain builders;
`buffer->string` is how a buffer becomes shareable.

This also gives a general explanation for the existing rule that freezes pairs
used as table keys. The implementation can use the same mechanism.

### Sending values

Sending has value semantics. The runtime traverses the message graph and:

- Reuses shareable values.
- Copies mutable pairs, arrays, tables, buffers, closure environments, and other
  copyable values.
- Preserves cycles and internal sharing within the copied graph.
- Rejects a graph containing a non-sendable value.

The sender retains its original graph. The receiver gets a distinct mutable
graph, so `eq?` is false for corresponding mutable objects in the two processes.
Sending to oneself follows the same rule. This keeps message semantics uniform
and makes object identity local to a process.

A send is atomic from Otium's point of view. The complete copied graph is
enqueued or nothing is enqueued. Failure must not leave a partially copied
message in the mailbox.

The cost of `send` is proportional to the mutable graph copied. Sending a PID,
reference, scalar, string, frozen value, or shareable function is O(1), aside
from the mailbox operation itself.

## Mutation and bindings

Concurrency does not require Otium to remove local mutation. Lexical `set!` and
the existing mutable container operations are safe when the containing process
is the only possible owner.

The unsafe part of the current model is the fallback from lexical `set!` to a
runtime-global namespace Var. A shared Var containing a mutable array is shared
memory. Even a shared Var containing an integer can be mistaken for an atomic
counter, although a read/modify/write sequence is not atomic.

The proposal separates two kinds of namespace binding.

### Process-local Vars

`defvar` creates or replaces a named binding owned by the current process:

```lisp
(defvar gold 0)
(set! gold (+ gold 1))
```

The binding lives in the process's local namespace table and persists until it
is replaced or the process exits. Unlike body-level `define`, `defvar`
always addresses that table, even when evaluated inside a function. A spawned
process can therefore initialize named state at the start of its root function.

For the first implementation, lexical bindings keep their current assignment
semantics. Function parameters, `let` bindings, and body-level `define`
bindings may still be targets of `set!`. Otium may later choose immutable
bindings by default and add an explicit mutable lexical form, but actor
isolation does not depend on that change.

The compiler classifies every symbol reference and assignment as lexical,
process-local, or published. It emits a separate opcode for each case, so the
VM does not perform a fallback search across binding kinds. The classification
uses lexical scope, `defvar` declarations, and normal published definitions.

A qualified name resolves the namespace first, then checks the current
process's local binding for that namespace before its published Var. Process
locals are not inherited by `spawn`. State needed by a child should be captured
lexically, constructed by the child, or sent in a message.

`set!` may assign a lexical binding or process-local Var. Attempting to assign a
published Var is an error. This permits a simple program to use `gold` directly
without making `gold` a shared atomic cell.

### Published definitions

Top-level `define`, `define-`, `defmacro`, and `defparam` publish definitions.
A namespace still holds stable Var objects, so existing compiled references see
a replacement immediately. Publication differs from ordinary assignment:

- The new value must be shareable.
- A single runtime publisher orders all updates.
- Readers see either the complete old value or the complete new value.
- There is no compare-and-swap, increment, or mutable payload access.

The module loader and host development connection have publication authority.
Ordinary spawned processes do not receive that authority automatically. A
later capability API may make publication available to application code that
needs it. Plain `(eval form)` runs in the caller's process and cannot publish
unless that process has been granted publication authority.

Other namespace-wide metadata changes use the same path. `define-condition`
updates the shared condition hierarchy through the publisher, and `ns`,
`require`, alias, refer, and module-load changes are serialized there. These
operations change program definitions, not process-owned application state.

Publishing a mutable value is an error rather than an implicit freeze:

```lisp
(define palette [1 2 3])
; error: cannot publish mutable value

(define palette (freeze [1 2 3]))
; accepted
```

Publishing a closure that captures a mutable lexical value fails for the same
reason. A normal top-level function closes over immutable bytecode and stable
namespace references and is shareable.

This keeps live redefinition without turning namespace Vars into an
application synchronization primitive. In the initial single-threaded runtime,
publication is naturally serialized. The distinction still matters because it
defines the behavior required of a future multi-threaded runtime.

## Process API

The names here are proposed API names, not reader syntax.

### Creation and identity

```lisp
(spawn thunk)
(spawn thunk options)
(self)
(pid? value)
(alive? pid)
(yield)
(sleep milliseconds)
```

`spawn` evaluates its arguments in the parent, copies the thunk's mutable
capture graph, creates the child, and places it on the runnable queue. The child
does not run inline before `spawn` returns. If the closure cannot be copied or
the runtime cannot allocate the process, no child is created and `spawn`
raises a condition.

The options table initially accepts `:mailbox-count`, `:mailbox-bytes`, and
`:name`. Unknown options are errors. Runtime configuration supplies conservative
defaults and upper bounds. A reasonable embedded prototype default is 32 user
messages and 16 KiB of copied mailbox payload per process, but those numbers are
runtime policy rather than language constants.

`yield` ends the current scheduling slice voluntarily. `sleep` parks the
process without blocking the OS thread. `alive?` is only a snapshot; code that
needs notification of death should use `monitor`.

### References

```lisp
(make-ref)
(ref? value)
```

A reference is an immutable, runtime-unique token. Request/reply protocols use
references to distinguish concurrent requests without relying on structural
pattern matching.

### Sending and receiving

```lisp
(send pid value)                    ; :ok, :full, :dead, or :not-sendable
(send! pid value)                   ; value, or raises on failure
(receive)                           ; block until one message arrives
(receive timeout-ms timeout-value)  ; timeout-value when no message arrives
```

Mailboxes are FIFO. Successful sends from one sender to one receiver retain
their order. The relative order of messages from different senders is the order
in which the runtime successfully enqueues them; programs should not infer a
stronger ordering.

`send` never blocks. Bounded mailboxes make accidental memory growth visible,
and a nonblocking result avoids introducing wait-for cycles into the primitive.
Libraries can implement retry, admission control, or a blocking protocol when
that is appropriate. `send!` is convenient when a full or dead mailbox is an
exceptional condition; on success it returns the value as evaluated in the
sender.

There is no selective receive in this proposal. `receive` removes the oldest
message. Code decodes it by hand, forwards it to another process, or reports a
protocol error.

The byte limit counts storage retained by copied message graphs. Shared frozen
values still consume an envelope and count against the message limit, but do
not charge their already-existing storage to every mailbox. Exact allocator
rounding is implementation-defined.

### Exit and termination

```lisp
(exit reason)
(kill pid reason)
```

Returning from the root thunk exits with `:normal`. `(exit reason)` performs an
uncatchable process-local unwind. It bypasses handlers and `try`, but runs
`unwind-protect` cleanups.

`kill` is a runtime control operation, not a user mailbox message. It marks the
target for hard termination at its next VM safepoint, discards the Otium
continuation, and does not run Otium cleanup forms. Dropping all process roots
reclaims its owned graph on a later collection. Native extension finalizers
still run according to the extension contract.

The distinction lets an application request a graceful shutdown through its
normal protocol and lets a task manager recover from a process whose Otium
cleanup code cannot be trusted. A process blocked in a synchronous native call
cannot reach a safepoint until the native call returns.

The existing session-level `exit` behavior will need a new name or host API.
`quit` should mean stopping the runtime session; `exit` should mean exiting the
current process.

### Inspection and registration

```lisp
(processes)
(process-info pid)
(register! name pid)
(whereis name)
(unregister! name)
```

`processes` returns a snapshot of live PIDs. `process-info` returns a small
frozen table containing at least the PID, optional name, status, mailbox count,
mailbox bytes, reductions, and exit state. It does not expose pointers into the
process heap.

The registry maps names to PIDs with runtime-defined atomic operations. It is a
specific coordination service, not a general shared table. Registrations are
removed when their processes exit. This is enough for an Otium task manager to
list work, report mailbox growth, and kill a selected process.

### Monitoring

```lisp
(monitor pid)       ; reference
(demonitor ref)     ; #t if an active monitor was removed
```

When the target exits, the monitoring process receives one system message:

```lisp
[:down ref pid reason]
```

Monitoring an already-dead PID produces the same notification. A monitor is
one-way and does not affect the target.

Down notifications use a runtime control lane rather than ordinary mailbox
capacity. The runtime reserves the small amount of notification storage when a
monitor is created, so a full user mailbox cannot silently hide a child crash.
The notification is presented by `receive` like an ordinary message. Runtime
and user envelopes receive sequence numbers at enqueue time, so the separate
storage does not discard their observable FIFO order.

Links and `spawn-link` are useful, but monitors are sufficient to build the
first supervisor library and have fewer policy questions. Links, exit trapping,
and restart intensity limits can be added after the monitor semantics are in
use.

### Supervision

A supervisor is an ordinary Otium process. It starts a child, monitors it, and
decodes `:down` messages. It may restart the same root function, stop after too
many failures, or report the failure to its own monitor. One-for-one and
one-for-all policies belong in an Otium library rather than the scheduler.

Nothing automatically treats a child as owned by its parent. `spawn` alone
creates no failure relationship. This avoids hidden shutdown cascades and lets
the supervisor library make the policy visible. A later `spawn-link` can be
convenient sugar once link behavior has been specified.

## Scheduling

The first runtime has one scheduler and one OS thread. Runnable processes sit in
a round-robin queue. A process runs until it:

- Uses its bytecode reduction budget.
- Calls `yield` or `sleep`.
- Blocks in `receive`.
- Exits or crashes.
- Is stopped by a runtime control request.

The reduction budget is based on bytecode execution rather than source-level
function calls. This prevents a CPU-bound calculation from making the game,
REPL, or supervisor unresponsive. The exact budget is runtime policy and may be
tuned without changing program results, apart from timing and otherwise-racy
message order.

`receive` with a timeout and `sleep` use a scheduler timer queue. No polling OS
thread is needed.

Scheduling safepoints and application control boundaries are different. A VM
safepoint is sufficient for preemption, collection, interruption, or hard
termination. It says nothing about whether an application's mutable state is
between two related updates. Live state edits therefore run at a control
boundary established by `receive` or `yield`, not at an arbitrary instruction.

The scheduler is preemptive with respect to Otium bytecode and cooperative with
respect to native code. A synchronous native function blocks the entire initial
runtime. Native APIs that can wait for input will eventually need nonblocking
integration or a worker facility. That is an embedding constraint, not a reason
to expose threads to Otium code.

## Failure, conditions, and dynamic state

Handlers, restarts, dynamic parameters, the current condition, and in-flight
unwind state belong to a process. A handler in one process never handles a
condition signalled in another. A child does not inherit its parent's active
handlers, restarts, or `with-params` bindings.

An uncaught condition becomes the process's exit reason after normal unwinding
and `unwind-protect` cleanup. A monitor receives a copied, bounded description
of that reason. If a condition contains a process-affine extension value or is
too large for the system-message limit, the runtime replaces the unsupported
portion with a printable summary.

The existing `interrupt` control transfer becomes process-targeted. An
interactive host may still establish `continue` and `abort` restarts for the
target process. Scheduler preemption itself is not an Otium condition and does
not run handlers.

`defparam` publishes an immutable parameter descriptor and a shareable default.
`with-params` installs values only in the current process. Those bound values
may be mutable because no other process can see the binding. A child starts
with parameter defaults unless the parent explicitly captures or sends the
desired value.

## Live development

Live definition replacement remains a language property.

Publishing a replacement updates one stable namespace Var. Existing function
activations continue executing their current bytecode. Each later global Var
load observes the currently published value. If a caller has already loaded a
function value, that call uses the loaded value. A subsequent global call may
use the replacement.

There is no definition epoch for a mailbox turn. Code that needs one stable
implementation during an operation can capture it lexically. Local recursion
should normally use a lexical loop name rather than repeatedly resolving the
published function.

Redefining a function containing an already-running loop does not replace that
activation. Long-lived processes should keep their outer loop small and call a
published handler on each event or frame. Redefining the handler changes later
events without replacing the PID, mailbox, or state.

Module loads and definition publication pass through one ordered publisher.
Publication is not a stop-the-world operation. A reader sees the old or new
complete value. Macro replacement affects only forms expanded afterward, as it
does in the current language.

### Conjure

The initial live-development feature is smaller than a debugger.

Conjure starts with the root process as its evaluation target. Definition forms
go to the code publisher. Other forms run in the target process at its next
`receive` or `yield` boundary and may read or change that process's named
process-local Vars. Evaluation has namespace scope, matching the current
`eval`; it does not select arbitrary lexical frames.

The stdio server can no longer block waiting for the next editor request while
application processes should be running. The hosted runtime should pump a
nonblocking control descriptor between scheduler slices, or let its embedding
host submit requests through `ot_step`/`ot_run` calls. This does not require an
Otium worker thread.

For a single-process program, this still looks like an ordinary live REPL:

```lisp
gold
; => 0

(set! gold 100)
```

With several processes, the client can select a PID or registered name before
evaluating. The request executes as that process. Conjure never receives a raw
reference to its mutable objects; returned mutable values are copied, and
printing should be bounded so inspecting a large game state does not duplicate
the heap accidentally.

The target does not interleave normal code with an injected form. An error in
the form is caught by the host evaluation boundary and reported without
crashing the target. This is not a transaction: mutations completed before the
error remain.

Frame selection, lexical-variable surgery, breakpoints, and a general heap
browser are deferred. The current interrupt break loop can remain available
during the transition.

## Examples

### Fibonacci service

The message protocol uses arrays and explicit tags because selective receive
and structural matching are out of scope.

```lisp
(define (fib n)
  (let recur ((k n))
    (if (< k 2)
        k
        (+ (recur (- k 1))
           (recur (- k 2))))))

(define (fib-server)
  (let loop ()
    (let ((message (receive)))
      (if (and (array? message)
               (= (length message) 4)
               (eq? (get message 0) :fib)
               (pid? (get message 1))
               (ref? (get message 2))
               (int? (get message 3)))
          (send! (get message 1)
                 [:fib-result
                  (get message 2)
                  (fib (get message 3))])
          (println "fib-server: bad message" message)))
    (loop)))

(defvar calculator (spawn fib-server))
```

A client sends its PID and a fresh reference:

```lisp
(define (ask-fib server n)
  (let ((ref (make-ref)))
    (send! server [:fib (self) ref n])
    (receive)))
```

This small client assumes it has no unrelated incoming messages. A practical
library can dedicate a client process to request demultiplexing.

Changing `fib` to an iterative implementation publishes new bytecode. The
lexical `recur` binding keeps an already-running calculation on the function
value it entered. A later request loads the new function. No process restart or
mailbox migration is involved.

### Roguelike

The simplest version can keep named state directly in the game process:

```lisp
(defvar gold 0)
(defvar depth 1)
(defvar player-x 5)
(defvar player-y 5)
(defvar running #t)

(define gold-per-pickup 1)

(define (collect-gold!)
  (set! gold (+ gold gold-per-pickup)))

(define (game-frame!)
  ...)

(while running
  (yield)
  (game-frame!))
```

Changing the current game state is a target-process evaluation:

```lisp
(set! gold 100)
```

Changing the rule for future pickups is publication:

```lisp
(define gold-per-pickup 100)
```

Redefining `game-frame!` affects the next frame because the small outer loop
loads it each time. The process keeps its window, terrain, mailbox, and current
state.

As the game grows, it may be useful to put terrain, position, inventory, and
render state in one process-owned table and pass that table to published
handlers. That is an application design choice, not a concurrency requirement.

## Changes to the current language specification

The current draft can be revised in place if this proposal is accepted. The
substantive changes are:

| Current section | Required change |
|---|---|
| Introduction | Replace "namespaces of mutable Vars" with the split between process-local mutable Vars and shareable published definitions. Add isolated processes as a core property. |
| Types and values | Add `pid` and `ref`. Define frozen collections, shareability, sendability, and process-affine extension values. |
| Equality | PIDs and refs compare by identity. Mutable identity is meaningful only within an owning process. |
| Complexity | Specify graph-copy cost for `send`, O(1) mailbox enqueue/dequeue after copying, and process-operation costs. |
| Symbol resolution | Insert the current process's namespace-local binding layer between lexical bindings and published namespace Vars. |
| Interruption | Replace "there are no threads" with process scheduling and targeted interruption. Separate scheduler preemption from language interruption. |
| Definitions and assignment | Add `defvar`. Restrict `set!` to lexical and process-local bindings. Require published `define` values to be shareable and route publication through the publisher. |
| Special forms | Add `defvar`, unless it is implemented in the prelude with equivalent semantics. |
| Namespaces | Keep stable published Vars, but remove ordinary mutation of their payloads. Describe publication authority and per-process local bindings. |
| Conditions and restarts | Make all dynamic condition state process-local. Define uncaught-condition process exits and hard termination. |
| Dynamic params | Make active bindings process-local and require a shareable published default. Do not inherit bindings at spawn. |
| Core library | Add process, mailbox, reference, monitor, registry, freezing, sleeping, and inspection functions. |
| Appendix | Add implementation latitude for reduction budgets, mailbox defaults, process limits, and native-call latency. |

The current mutable pairs, arrays, tables, and buffers remain mutable. Their
mutation functions gain two checks: frozen values reject mutation, and native
debug builds may assert that the current process owns the object.

## Changes to the current runtime

The present runtime has one `ot_state` and one bytecode continuation. VM frames
and the operand stack live in runtime-managed storage and are collector roots.
The current namespace, handlers, restarts, parameters, conditions, and
interrupt state still belong to the whole runtime. Namespace Vars accept
arbitrary values, and `set!` writes them directly.

That is enough for bytecode parity, but not for suspending several independent
evaluations. The process work moves the existing continuation and dynamic state
into per-process records.

### Runtime-wide state

`ot_state` remains the embedded runtime and owns:

- The heap and collector.
- Interned names, namespaces, modules, and published Vars.
- The code publisher.
- The process registry and PID generation counter.
- Runnable, timer, and control queues.
- Host writer, loader, and extension type registrations.
- The currently executing process pointer.

Runtime configuration gains limits for process count, default and maximum
mailbox budgets, monitors, timers, initial VM stack size, and bytecode
reductions per scheduling slice. Storage for a configured maximum is not
reserved eagerly.

The implementation stays platform-agnostic in `otium.c`. Clock, polling, wakeup,
and other host-specific scheduler operations belong in the platform file; the
POSIX implementation remains in `ot-posix.c`.

The fields that describe an evaluation move from `ot_state` into an
`ot_process` structure: current namespace, bytecode frames, dynamic stacks,
unwind state, condition, interrupt state, process-local Vars, mailbox, monitors,
and reduction counters.

### Bytecode VM

Each process needs a resumable VM stack stored in runtime-managed memory rather
than on the C stack. At minimum the VM needs operations for local, process-local,
and published Var access; call and tail call; condition transfer; send;
receive-and-park; yield; and periodic budget checks.

The current bytecode format uses printable ASCII opcode bytes and four ASCII
hex digits for each operand. Constants live in a separate pool. Functions hold
bytecode, constants, parameters, and closure state; they do not retain an AST
body. Keep that representation when process opcodes are added so compiled
functions remain straightforward to serialize.

Native calls may continue to receive `ots*` and obtain the current process from
it. They must not retain an unrooted mutable `otv` for later use by another
process.

The existing `ot_eval_src` API can remain as a compatibility convenience. It
submits work to the root process and runs the scheduler until that request
finishes or the runtime becomes idle. New embedding APIs should expose
single-step/run-until-idle operation plus spawn, send, inspect, interrupt, and
kill requests.

### Mailboxes and copying

The runtime needs an iterative graph copier with a temporary source-to-copy
map. The map preserves aliases and cycles, applies mailbox byte accounting, and
keeps both source and partial destination rooted across allocation. The same
copy policy can serve `spawn` capture copying and host-to-process value
delivery.

Mailbox envelopes and process metadata should use small host allocations or
compact nonmoving runtime records. Mailbox storage, process-local Var tables,
and monitor tables should be allocated lazily. A process that only computes and
returns should not pay for all of them.

### Definitions

Published Vars remain stable objects, but their values pass a shareability
check. In the single-threaded runtime, the publisher is a scheduler control
queue. A future multi-threaded runtime may use an atomic pointer replacement or
publisher lock without changing the semantics.

Process-local Vars are a sparse table keyed by namespace and name. Published
bytecode can resolve one at call time in the executing process. There is no
per-process copy of every namespace Var.

### Conditions and control

The C records for handler, restart, and dynamic-parameter stacks become part of
the active `ot_process`, or become bytecode-frame records. Process crashes drop
only that process's continuation. Monitors and the scheduler retain the small
exit record long enough to deliver notifications and answer inspection calls.

User mailboxes and runtime control requests are separate. Interrupt, kill,
monitor-down delivery, publication, and Conjure evaluation must not depend on
space in a target's ordinary mailbox.

### Extensions

The extension type registration API needs a transfer policy:

- `shared`: all values of the type are immutable and may cross unchanged.
- `copy`: the extension supplies a clone callback.
- `affine`: the value belongs to its creating process and cannot cross.

Affine should be the default. Raylib windows, render textures, fonts, and
similar host handles should remain with the rendering process. The runtime may
later support pinning that process to the OS main thread; no pinning mechanism
is required for the single-threaded scheduler.

Heap isolation does not serialize the outside world. Two processes can still
write the same file, device, or output sink through native code. In the initial
runtime, native calls are serialized because there is one scheduler thread.
An extension marked `shared` must define its own thread-safety behavior before
M:N execution is enabled.

The initial concurrency work stops before changing this API or porting the
roguelike and extension examples. Those are follow-up work after process value
transfer is settled.

The current global-root API must reject mutable process-owned values. Native
code needs a process-root API for values retained on behalf of one process.

### Collector

This proposal does not select or require a collector algorithm. The initial
implementation may keep one global heap and the existing collector. Collection
stops the one scheduler thread, traces published roots plus every live process's
VM stack, local Vars, mailboxes, and dynamic state, then resumes scheduling.

The ownership model is still useful with one physical heap. It constrains which
references the runtime may create; it does not require one heap or collector per
process. Replacing the collector later should not affect process semantics.

### M:N later

The single-threaded scheduler is a complete implementation of the language
model, not a different concurrency mode. A later M:N runtime adds worker
threads, work distribution, publisher synchronization, and a collector
coordination strategy. Otium code continues to use the same PIDs, mailboxes,
ownership rules, and failure behavior.

Avoiding a C-stack continuation per process now is the main architectural step
that keeps this route open.

## Implementation sequence

### 1. Bytecode parity (complete)

The bytecode VM runs the existing language and extension tests in one root
execution. Functions contain printable ASCII bytecode instead of AST bodies,
and lexical and published lookups use distinct opcodes. The collector traces
VM frames and the operand stack. Moving dynamic state into `ot_process` belongs
to the next step because it needs the process lifetime and unwind model.

### 2. Isolation primitives

Add PID and ref values, `ot_process`, the round-robin scheduler, process-local
Vars, shareability checks, `freeze`, the graph copier, bounded mailboxes,
`spawn`, `send`, `receive`, `yield`, timers, and process-local exit.

Tests at this point should demonstrate:

- A mutable message is not aliased with its sender.
- Cycles and internal sharing survive a send.
- Publishing a mutable graph or mutable-capturing closure fails.
- Mailbox rejection is atomic.
- A CPU-bound process cannot starve another Otium process.
- An uncaught condition terminates only its process.

### 3. Management and supervision

Add monitor records with reserved notifications, the process registry,
inspection snapshots, hard kill, and a small Otium supervisor library. Build a
task-manager example that lists processes, shows mailbox use, and kills one.

### 4. Live development

Route definition forms through the publisher. Extend the stdio server and
Conjure client with target-process selection and boundary evaluation of named
process-local Vars. Keep interrupt/continue/abort while this settles.

### 5. Deferred work

After the single-threaded model has real programs behind it, revisit links and
exit trapping, selective receive or protocol helpers, M:N scheduling,
nonblocking native integration, process memory accounting beyond mailboxes, and
collector design.

## Open decisions

The proposal intentionally leaves a few names and policies unsettled:

- Whether lexical bindings eventually become immutable by default.
- The default mailbox count and byte budgets for the hosted and embedded
  builds.
- The exact capability by which application code may publish definitions.
- Whether links should enter the first supervisor API or follow monitors.

None of these changes the isolation invariant or requires M:N threads or a new
collector.
