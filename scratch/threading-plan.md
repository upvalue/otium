# Green threads plan

Goal: Gambit-style green threads in both runtimes. Threads can block on
timers, mutexes, condition variables, mailboxes and I/O without stopping
the process, and can optionally be preempted. The scheduler and the
thread API are written once in Otium and shared. Each runtime supplies a
small set of primitives under it.

## Where things stand

- The C runtime has one-shot continuations and per-thread VM stacks
  (commit 9eb1ccc). A thread switch (`%swap`) takes about 47 ns and runs
  no cleanups. Each stack keeps its own handlers, restarts, params and
  cleanup list.
- `lib/otium/thread.ot` is cooperative only: spawn, yield!, join!,
  send!, receive!, run-all!. It has no timers, no timeouts and no
  blocking I/O, and it runs on C only.
- Chez has neither call-1cc nor otium.thread (lan-p467). All Otium
  dynamic state there is Chez parameters and `dynamic-wind`, so a switch
  either runs everyone's cleanups or leaks bindings between threads.
- I/O is blocking in both runtimes. In C, port reads happen inside
  natives (`port_ensure`), and a thread can't switch inside a native.

## Decisions so far

- One-shot continuations only. No multi-shot call/cc.
- Thread switches run no cleanups. Escapes with call-1cc do.
- Chez gets pinned to a known version, so relying on Chez internals
  (`#%$current-winders`) is acceptable, guarded by a test.
- I/O follows Gambit's Unix model first: every fd non-blocking, try the
  operation, park the thread on "would block", one idle wait over all
  fds and the next timer deadline. Regular files go through the same
  path and can still stall the process on a disk miss, as in Gambit.
  A worker pool or per-port helper threads are deferred until those
  stalls are a real problem.
- The idle wait belongs to the host, so Manse can run it inside its own
  event loop.

## Stage 0: prototype the risky parts

Small throwaway slices, to find out whether the approach holds before
building on it.

1. Chez switch with no cleanups: `call/1cc`, plus swapping a per-thread
   state record, `#%$current-winders` and, if it leaks the same way,
   Chez's exception handler stack. Check that a thread yielding inside
   `with-params`, `try` and `unwind-protect` keeps its own bindings and
   runs no cleanups. Check whether Chez's handler stack leaks
   (lan-p467 says unchecked).
2. C retry of a blocking native: one port read on a pipe returns
   "would block", the thread parks, another thread writes, the read
   is re-run and succeeds.

## Stage 1: Chez parity for what C has now

Covers lan-p467.

- Replace the Chez parameters behind Otium dynamic state with one
  record per thread: handlers, restarts, control-target, the params
  list (defparam and with-params), and current input, output and error
  ports. Reading a param becomes a list walk, as in C.
- `%new-stack`, `%swap`, `%switch`, `%resume`, `%can-swap?` and
  `call-1cc` on Chez, built on `call/1cc` and the state swap.
- Decide what to do with the runtime's internal Chez parameters
  (`current-namespace`, `source-path`, `current-expander`,
  `expansion-budget`, `current-top-form`). Chez's `parameterize` stores
  the value in the shared parameter, so swapping winders doesn't help.
  Either move them into the record or forbid switching during load and
  macro expansion. This matters more once preemption exists.
- Measure the switch cost and param reads on `building-children` in
  `otium.ui` and the port params.
- Done when `tests/library/threads-c.ot` and `continuations-c.ot` pass
  on both runtimes, and are renamed as shared tests.

## Stage 2: the shared scheduler

Otium only, apart from a clock and a sleep primitive in each runtime.

- A thread record: continuation, state (runnable, blocked, done),
  result or condition, mailbox, a field for user data.
- A run queue and a timer queue sorted by deadline.
- `sleep`, and timeouts on join, receive and mutex lock. A thread
  waiting with a timeout sits on both the timer queue and the thing it
  waits for. Whichever fires first wins and removes the other entry.
- Mutexes and condition variables, with wait queues like join's
  waiters.
- Interrupting a thread with a thunk: a switch hands over an optional
  action, which the resumed thread runs in its own dynamic context.
  Terminating a thread is an interrupt that raises, so the target's
  own cleanups run on its own stack.
- When nothing can run: sleep until the next deadline, or report
  deadlock if there is none.

## Stage 3: blocking I/O

### Host interface (both runtimes)

`ot_io_host` (`c/src/otium.h:213`) is synchronous. Add:

- non-blocking read and write that can return a "would block" error
- setting an fd non-blocking at open, as Gambit does
- a wait over a set of fds and a deadline, which the host can replace
  (Manse)

### C runtime

- A new call status beside `CALL_SWITCHED`, meaning "this native would
  block on fd". The native records the fd and direction and returns a
  sentinel.
- When `vm.floor == 0`, the VM keeps the call and its arguments, pushes
  a retry marker, and calls the scheduler's wait function. When the
  wait returns into the marker, the original call is re-run.
- When `vm.floor > 0` (inside a native callback), there is no switching,
  so the native falls back to waiting on that one fd in the host. The
  program is slower there but still correct.
- The retry only works if a native is safe to re-run. `port_ensure` is:
  it fills the buffer and consumes nothing until it succeeds. Writes
  must keep track of how much of the buffer has gone out, on the port.
- Natives called from the hot loop (`vm_direct`) either handle the new
  status or port natives stay off that path.
- Move custom port callbacks onto the VM stack, so a custom port whose
  read waits on another thread works.

### Chez

- Otium's ports are Scheme code wrapping Chez ports (`chez/otium/ports.sls`),
  so a read can call the scheduler's wait directly, with no retry
  mechanism needed.
- Getting non-blocking reads: either Chez's `set-port-nonblocking!` on
  fd ports, or FFI calls to the C host's read, write and wait. Check
  what Chez returns from a non-blocking read with no data before
  choosing. Sharing the C host code would give both runtimes the same
  wait loop.

## Stage 4: preemption

- C: a second atomic flag next to `vm.interrupted`, set by a host timer,
  or a clock check at the existing poll (`c/src/otium.c:4881`). At the
  poll, when the flag is set and `vm.floor == 0`, push a `FRAME_PREEMPT`
  marker and call the scheduler's yield. The marker throws away the
  result and the interrupted frame carries on. When `floor > 0`, leave
  the flag set and try at the next poll.
- A no-preemption counter in the VM, held by the scheduler while it
  changes its queues and released by the thread it switches to.
- Chez: `timer-interrupt-handler` with `disable-interrupts` and
  `enable-interrupts` for the scheduler. The dev watchdog already owns
  the timer (`chez/otium/runtime.sls:74-90`, `dev-tick`), so both go
  through one handler.
- Opt-in at first, per thread or per scheduler. Existing code assumes a
  run of forms without a yield can't be interrupted. Natives are atomic
  either way, since preemption only happens between instructions.

## Stage 5: remaining native callbacks in C

A thread inside one of these can't block or be preempted:

- handlers called by `signal`
- `try` catch predicates and the binding expressions of `with-params`
  and `handler-bind`, which still run through `vm_run_code`
- generic method chains with `next-method`
- cached slot accessors, class mount, macro expansion, FFI callbacks,
  the interrupt hook

Move them onto the VM stack in roughly that order.

## Later

- A worker pool (or Gambit-style per-port helper threads) for file
  reads that miss the disk cache, and for `open`, `fsync`, directory
  listing, `getaddrinfo` and waiting on child processes. On Linux,
  `preadv2` with `RWF_NOWAIT` avoids the pool for cached reads. Workers
  can't touch the GC heap: port buffers are nursery bytevectors and
  move, so a worker reads into its own buffer and the main thread
  copies.
- Trimming parked stacks: at 10 frames deep a parked C thread costs
  about 1.6 KB, mostly the values array.
- Priorities.

## Open questions

- Preemption on by default, or opt-in?
- Who owns the idle wait in Manse: the Otium scheduler or Manse's loop?
- SRFI-18 names (`mutex-lock!`, `thread-sleep!`, ...) or Otium's own?
- How Chez gets pinned.
