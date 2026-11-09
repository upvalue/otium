# ✨ Concurrency

Use threads for work that finishes with a result. Use actors for a service that
keeps state and handles requests over time. An actor is a thread with a mailbox;
both use `otium.thread` to spawn work.

Otium threads interleave on one native thread. They do not run CPU work on
several cores. Threads require native execution and are unavailable with
`--c.vm`. Waiting for a thread, message, timer, or supported file-descriptor I/O
lets other threads run. Foreign calls and custom port callbacks can block all
threads.

## Start work and collect results

`thread/spawn` takes a function and its arguments. It returns a thread
immediately. `thread/join` waits for that thread and returns its result.

```otium
(require otium.thread :as thread)

(let ((a (thread/spawn (fn (n) (* n n)) 6))
      (b (thread/spawn (fn (n) (* n n)) 8)))
  (+ (thread/join a) (thread/join b)))
;; => 100
```

Start both workers before joining either one. A spawned thread inherits the
current namespace and [dynamic parameter bindings](params.md).

Threads get turns automatically. Use `thread/sleep` to wait a number of seconds,
including fractional seconds. `thread/yield` lets other runnable threads go
first; neither is a substitute for waiting for a result.

## Handle completion and failure

Pass a timeout in seconds as the second argument to `thread/join`. If it
expires, join raises a condition recognized by `thread/timeout?`. The worker
continues running.

If a worker fails, joining it raises a condition recognized by `thread/failed?`.
Its `:reason` holds the original condition:

```otium
(let ((worker (thread/spawn (fn () (error "work failed")))))
  (try
    (thread/join worker)
    (catch (thread/failed? problem)
      (condition-message (get problem :reason)))))
;; => "work failed"
```

To cancel work, use `(thread/kill! worker)`, then join it to wait for cleanup.
Killing runs cleanups and cannot be caught with `try`. Joining a killed thread
returns its kill reason, which defaults to `:killed`.

For related workers that must finish before a scope ends, use a group:

```otium
(let ((results []))
  (with ((g (thread/group)))
    (thread/spawn-in g (fn () (push! results :ready))))
  results)
;; => [:ready]
```

Leaving `with` waits for the group's workers. By default, a worker failure kills
its siblings, interrupts the group's owner, and raises a failure with the
original condition under `:reason`.

## Keep state in an actor

Load `otium.actor` for request/reply calls. Spawning, sending, and receiving
still come from `otium.thread`.

```otium
(require otium.actor :as actor)
(require otium.thread :as thread :refer (spawn send receive))

(def (serve count)
  (serve
    (receive
      ({:op :add :n n} (+ count n))
      (:call {:op :get} count))))

(let ((counter (spawn serve 0)))
  (defer
    (begin
      (send counter {:op :add :n 2})
      (actor/call counter {:op :get}))
    (thread/kill! counter)
    (thread/join counter)))
;; => 2
```

`send` appends a message without waiting for it to be handled. It returns `#f`
if the target has finished.

`receive` takes the first message matching one of its [patterns](matching.md).
Unmatched messages stay in the mailbox. Add `(:after seconds body ...)` to stop
waiting after a timeout.

The counter keeps its state in the `count` argument. Each ordinary message
computes the next state. A `:call` clause sends its body's value back to the
caller and also returns that value from `receive`.

`actor/call` waits for a matching reply. Its optional third argument is a
timeout in seconds; the default is five seconds. Passing `nil` waits
indefinitely. A timeout raises a condition recognized by `thread/timeout?`. It
does not cancel a handler that has already started.

## Observe actor failures

An error inside a `:call` body is raised in the caller. The actor's `receive`
keeps waiting for another message, so a failed request need not end the service.

An uncaught error outside a `:call` body ends the actor. `send` does not report
handler errors, and a pending call can time out if the actor dies without
replying. Use a monitor when you need notification that an actor has ended:

```otium
(let ((worker (spawn (fn () (error "worker stopped")))))
  (let ((ref (actor/monitor worker)))
    (receive
      ({:exit _ :ref (== ref) :reason reason}
       (condition-message reason)))))
;; => "worker stopped"
```

A monitor sends an exit message even if the thread has already ended. Its
`:reason` is `:normal` for normal completion, the kill reason for cancellation,
or the original condition for failure.

## Choose how to share state

Use a worker and `join` when you need one result. Use a group when several
workers belong to one task.

Use an actor when several callers need access to state that one service can own.
Keep that state inside the actor and communicate through messages.

If threads must directly share mutable state, protect related reads and writes
with a mutex from `thread/mutex` and `thread/with-lock`. The lock is released
even when the body exits with an error. A single native thread does not make a
multi-step update safe from interleaving.

<!-- generated from concurrency.md.spec sha256:82b2a3bc5f7f3b1625d952f660e47e5e09cfa90903e06057fbcc4e9e727f6958 -->
