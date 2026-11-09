# ✨ Concurrency

Concurrency lets one task make progress while another waits for input, a
message, or a timer. Otium threads take turns on one native thread. They help
with waiting, not with using more cores. Foreign calls and custom port callbacks
can still block all threads.

`otium.thread` provides threads: running function calls whose results you can
wait for and collect. Each thread also has a mailbox, a queue of incoming
messages. An actor is a thread that serves messages from its mailbox, often
keeping state between requests. `otium.actor` adds request/reply calls and
notifications when a thread ends.

Threads require native execution; they are not available under `--c.vm`.

## Start work and collect its result

Use `thread/spawn` to start a function with arguments. It returns a thread
immediately. `thread/join` waits for that thread and returns the function's
result.

```otium
(require otium.thread :as thread)

(def (work value)
  (thread/sleep 0.01)
  value)

(let ((a (thread/spawn work 20))
      (b (thread/spawn work 22)))
  (+ (thread/join a) (thread/join b)))
;; => 42
```

Both jobs start before either is joined. Sleeping or joining pauses only the
calling thread, allowing others to run. Sleep durations are in seconds.

Otium switches between runnable threads automatically; you do not need to insert
`thread/yield` into every loop.

Threads share mutable values, and their operations can interleave even on one
native thread. If several threads update the same state, protect the update with
a `thread/mutex` and `thread/with-lock`, or give one actor responsibility for
that state.

## When a thread fails

An uncaught error ends the thread with status `:failed` and prints a diagnostic
to standard error. It does not automatically raise an error in the thread that
spawned it.

Joining a failed thread raises a `thread-failed` condition. Its `:reason` holds
the original error:

```otium
(let ((job (thread/spawn (fn () (error "work failed")))))
  (try
    (thread/join job)
    (catch (thread/failed? problem)
      (condition-message (get problem :reason)))))
;; => "work failed"
```

Catching the join error does not suppress the diagnostic.

You can give `thread/join` a timeout in seconds. A timeout raises a condition
recognized by `thread/timeout?`; it does not stop the job.

To stop a thread, use `thread/kill!`. Its cleanups run, but `try` cannot catch
the termination. Joining it returns the kill reason, which defaults to
`:killed`.

For tasks that should succeed or fail together, use `thread/group` inside `with`
and start children with `thread/spawn-in`. Leaving the group waits for its
children. By default, a child failure stops its siblings and interrupts the
owning body. The failure is reported when the group closes.

## Serve messages with an actor

Actors use the same `thread/spawn` as other threads. A typical actor loops
around `thread/receive`, carrying its state into the next iteration.

`thread/send` queues a message without waiting for it to be handled.
`thread/receive` takes the first matching message and evaluates that clause.
Unmatched messages stay in the mailbox. If no message matches, it waits.

For a reply, use `actor/call`. The actor handles the request with a `:call`
clause, whose result is sent back automatically.

```otium
(require otium.actor :as actor)

(def (serve count)
  (serve
    (thread/receive
      ({:op :add :n n} (+ count n))
      (:call {:op :get} count)
      (:call {:op :fail} (error "request failed")))))

(def counter (thread/spawn serve 0))

(thread/send counter {:op :add :n 2})
(actor/call counter {:op :get})
;; => 2
```

Each successful clause returns the count for the next iteration. The actor
handles updates one at a time, so callers need not lock the counter's state.

A successful send means the message was queued; it does not tell you whether the
work succeeded. Sending to a finished thread returns `#f`.

## Request failures and actor exits

An error inside a `:call` clause is raised in the caller. The actor's `receive`
continues waiting for another message:

```otium
(try
  (actor/call counter {:op :fail})
  (catch problem (condition-message problem)))
;; => "request failed"

(actor/call counter {:op :get})
;; => 2
```

Mutations made before the error are not rolled back.

By default, `actor/call` waits up to five seconds. Supply a third argument to
change the timeout. A timeout raises a `thread/timeout?` condition; it does not
cancel work already underway. If the actor dies without replying, the call can
time out rather than immediately reporting its exit.

An uncaught error outside a `:call` handler ends the actor like any other
thread. Use `actor/monitor` when you need notification of its exit. A monitor
returns a reference and sends your mailbox an exit message:

```otium
(let ((ref (actor/monitor counter)))
  (thread/kill! counter :shutdown)
  (thread/receive
    ({:exit who :ref (== ref) :reason reason} reason)))
;; => :shutdown
```

The exit reason is `:normal` for normal completion, the supplied reason for a
kill, or the original condition for a failure. Monitoring reports termination;
it does not restart the actor.

## Which should you use?

Actors are threads, so choosing between them is a matter of organizing work and
state. Neither choice lets you use more cores.

- Use a thread for a task with a beginning, an end, and a result you will join.
- Use an actor for a service that handles repeated requests or owns changing
  state.
- Use a thread group when several tasks belong to one operation and should fail
  together.

<!-- generated from concurrency.md.spec sha256:1cfe3155072c00e0f54e30a9880102eeffd2b67aa0f29d28c9724408570d5d5c -->
