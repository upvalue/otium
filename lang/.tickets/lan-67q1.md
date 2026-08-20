---
id: lan-67q1
status: closed
deps: []
links: [lan-9yo3, lan-p1e2, lan-5je1]
created: 2026-08-16T15:46:48Z
type: epic
priority: 2
assignee: Phil
tags: [concurrency, vm, gc, design]
---
# Concurrency: Erlang-style processes, design discussion notes

Notes from a long design discussion, compressed. No decisions encoded here; the open questions at the bottom are the point.

## Goal

Erlang/Go-shaped concurrency: processes that communicate by message and avoid shared mutable state. Eventually m:n threading. A short-term stage that interleaves processes on one thread is acceptable, but purely cooperative fibers (Lua-style) are ruled out: a supervisor process must be able to manage a wedged child, which requires preemption the child can't opt out of. Full Clojure-style (shared heap, immutable-by-default, STM) was discussed and set aside: it needs a concurrent moving GC we don't want to build, and it has no fault-isolation story. Persistent data structures and atoms over a shared immutable region may come back later; immutable-by-default is pinned for now.

## What the runtime already gives us

Surveyed the VM; it is unusually well shaped for this:

- Zero global mutable state. Heap, intern, namespaces, unwind state all live in State. Tests already run multiple States.
- Otium->Otium calls run in one flat loop over explicit heap-allocated CallFrames with real TCO (vm.cpp). Suspending pure Otium code is nearly free.
- VM_POLL_INTERRUPT fires on back-edges and calls (spec 3.6). That is an Erlang reduction-count hook: preemption at safepoints, no OS threads needed.
- Unwinding is data (sentinel Value + fields on State), not longjmp. Conditions map naturally onto exit reasons / links / supervision later.
- No blocking syscalls in the core; all I/O is two host callbacks. The I/O layer can be designed message-shaped from scratch.

The one blocker: the C stack is load-bearing in two places. Natives that re-enter Otium do so via nested vm_execute, and every dynamic-extent form (try, unwind-protect, handler-bind, restart-case, with-params, ns, require) compiles to a native call running its body as a thunk (compile.cpp ~955-1230, eval.cpp ~194-380). A process can't yield with any of those live.

## The Erlang model, as it applies here

- A process = heap + stack + frames + mailbox + pid + links + reduction counter. Ours is the per-execution fields of State factored into a struct, plus a mailbox. A suspended process must be pure data (see the fork below).
- Scheduler: budget of reductions, decrement at the existing poll points, save-and-requeue on exhaustion. Infinite loops still yield because you can't loop without a back-edge or call. Natives are uninterruptible until they return; Erlang's answer is dirty scheduler threads, ours would be too, later.
- Send deep-copies into the receiver's heap; never blocks, send-to-dead discards (Erlang's choice; Go chose blocking sends -- open question). Copying is what keeps heaps private and GC per-process. collectInto is already a deep-copy engine and should be reusable for send and for spawn-from-template (which also dodges per-process prelude bootstrap cost).
- Receive is selective (scan mailbox for first match, block if none, timeout via timer wheel). Could simplify to queue + predicate.
- Failure: exit signals along links, default is transitive death, trap_exit turns signals into messages. Supervisors are just a library on top. Our conditions give exit reasons structure for free.
- Per-process GC: collections touch one small heap, pause one process, dead process = free(). This is the low-memory win.
- m:n later is scheduler-internal: one run queue per OS thread, work stealing, migration is moving a pointer, because processes are self-contained. Semantics get fixed in the m:1 stage; threads don't change them.
- I/O: scheduler threads never block; readiness becomes messages. Blank I/O surface makes this easy for us.

## Shared code and vars

BEAM has a third region: non-moving, immutable-after-load code area + literal pool. Pointers into it from process heaps are fine because it never moves. Plan-shaped equivalent: hoist Code objects, constants, and the intern table into a pinned shared region; the GC walker treats out-of-region pointers as terminal (filed: lan-9yo3).

Erlang has no mutable globals; we have vars. Three options discussed:

1. Per-process vars, forked at spawn (Dart/JS-worker style). Clean, but set! silently not propagating will surprise, and global reload stops meaning anything.
2. Shared read-mostly registry; reads are pointer loads; define/set! publishes an immutable value via atomic swap -- every var a micro code-upgrade. Prior art: Erlang persistent_term (near-exact match), RCU, Clojure vars, BEAM code loading. All of them say the same thing: publication is easy, reclamation of the superseded value is the actual problem (persistent_term scans all process heaps on delete; RCU uses grace periods). v0 can leak superseded values, or in m:1 do a synchronous scan at set! time. Constraint: var values must be shareable, i.e. effectively immutable from the reader's side. Mostly they're functions, so mostly fine; closures over mutable state need defining.
3. Vars owned by a process, access by message. Too slow for prelude lookup.

Lean was (2), also because it gives hot code reload, which fits the malleability goal. Not decided.

## Collector

Per-process semispace Cheney is close to what BEAM itself runs (young gen is a Cheney copy). Suitable. Adaptations discussed but deferred: allocate to-space at collect time so the 2x is transient not resident; start heaps tiny, grow gently, shrink, per-process max as a supervision tool; maybe an old generation later if recopying shows up. Only the walker region check is filed (lan-9yo3); the rest waits until the design lands.

## The stackful vs pure-data fork

Two ways to handle the C-stack blocker, and it's the biggest open decision:

- Reify: dynamic extents become markers on the VM frame stack; natives re-entering get restricted or trampolined. Processes become pure data: ~1KB suspended, inspectable, serializable, trivially migratable. Cost: the refactor itself, and permanent constraints on natives/JIT calling back in. Overlaps lan-5je1 (backtraces also touch CallFrame).
- Stackful: each process gets its own small C stack (16-64KB, guard page); scheduler context-switches onto it (minicoro-style, single-file assembly switcher, matches our vendoring posture). Nested vm_execute, native frames, JIT frames all just stay put across a yield. Still preemptive -- switch policy is the scheduler's, at the same poll points; Go proves the combination. Costs: per-process memory floor (pages, not KB), stack overflow as a new failure mode, thread-migration sharp edges (TLS assumptions) in m:n.

Hybrid is viable: stackful first to ship semantics, reify later to shrink the floor; scheduler/mailboxes/signals are identical above the switch mechanism.

## Interaction with the JIT (lan-p1e2)

Mostly mutual reinforcement:

- The pinned code region is something native code needs anyway (executable pages, MAP_JIT on macOS); it also gives the JIT stable code addresses instead of the movable-CodeData workaround.
- Per-process GC deletes global safepoint coordination; JIT'd code keeps the interpreter's rooting discipline and nothing more. Only shared-region ops (var reclamation, code purge) need everyone at poll points.
- Poll points unify: Ctrl-C, JIT interrupt checks, reduction counting -- one placement rule.
- Var option 2 pins var cells, answering lan-p1e2's open question about baking cell addresses.
- JIT compiles are long uninterruptible natives -> dirty pool; publish jitEntry atomically.

One real collision: lan-p1e2 has non-tail JIT->JIT calls recursing on the C stack. Fine for Ctrl-C (unwind-only), incompatible with pure-data suspension. Under reify, all JIT calls go through the trampoline (or deopt-at-yield machinery); under stackful, the collision vanishes and the JIT keeps native call/ret. So the process representation and the JIT calling convention are one decision, not two.

## Libraries

Core (scheduler, processes, mailboxes, links, send-copy) is ours to write regardless; each piece is hundreds of lines on structures we have. Vendor timeout.c (wahern) for the timer wheel. minicoro only if stackful wins. moodycamel MPSC queue only if m:n profiling asks. libuv when real networking arrives; hand-rolled kqueue/epoll or libev-sized before that. Actor frameworks (CAF etc.) and GC libraries are the wrong shape; the closest prior art overall is the BEAM itself, as reference not library.

## Open decisions

1. Stackful vs reified extents, decided jointly with the JIT calling convention.
2. Var semantics (lean: shared + publish; reclamation strategy).
3. Mailbox + selective receive vs channels; send backpressure or fire-and-forget; send-to-dead semantics.
4. Pids as capabilities vs forgeable.
5. Spawn-from-template mechanics and what a process inherits.
6. Staging: extents/region -> m:1 preemptive scheduler + spawn/send/receive -> links/monitors/trap -> I/O layer -> dirty pool -> m:n. Each stage useful on its own; semantics frozen at m:1.

Spec note: spec.md 3.6 currently says "there are no threads" normatively; this is a spec change when it happens.

## Notes

**2026-08-20T03:47:55Z**

Dropping the in-runtime Erlang-style process design. The bytecode VM remains synchronous; future concurrency should manage independent ots* states outside the core runtime.
