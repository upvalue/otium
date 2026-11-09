# ✨ Concurrency

Concurrency lets other work continue while one task waits for input, a timer, or
a reply. Otium threads take turns on one native thread. They help with waiting,
not with using more cores.

A thread runs a function and holds its eventual result. Each thread also has a
mailbox: a queue of incoming messages. An actor is a thread that serves messages
from its mailbox, usually keeping state between requests.

Use a thread for a task that finishes with a result, and an actor for an ongoing
service that owns state and handles requests.

## Start work and wait for it

[otium.thread](/reference/otium.thread/) provides threads.
[`spawn`](/reference/otium.thread/#spawn) starts a function with the supplied
arguments and returns a thread handle. [`join`](/reference/otium.thread/#join)
waits for that thread and returns its result.

```otium
(require otium.thread :as thread)

(def (later value)
  (thread/sleep 0.01)
  value)

(let ((a (thread/spawn later 20))
      (b (thread/spawn later 22)))
  (+ (thread/join a) (thread/join b)))
; => 42
```

Both tasks start before either is joined. Sleeping or joining parks only the
waiting thread. Waiting for mailbox messages, locks, or file-descriptor I/O also
lets other threads run. Foreign calls and custom port callbacks can block all
threads.

Threads take turns automatically, so you do not need to insert `yield` calls
into every loop. They require native execution and are unavailable under
`--c.vm`.

Threads share mutable values. If several threads update the same state, protect
the operation with [`with-lock`](/reference/otium.thread/#with-lock) and a
shared [`mutex`](/reference/otium.thread/#mutex), or give one actor
responsibility for that state.

## Thread failures and cancellation

An uncaught error ends the thread with status `:failed`. An ordinary spawned
thread does not automatically fail its parent. Joining it raises a
`thread-failed` condition whose `:reason` contains the original condition.

```otium
(let ((worker (thread/spawn (fn () (error "work failed")))))
  (try
    (thread/join worker)
    (catch (thread/failed? problem)
      (condition-message (get problem :reason)))))
; => "work failed"
```

The failed thread also prints an error diagnostic. See [Errors and
recovery](errors.md) for handling conditions.

A timeout passed to `join` is in seconds. It raises a condition recognized by
[`timeout?`](/reference/otium.thread/#timeout?) without cancelling the worker.

To cancel a thread, use [`kill!`](/reference/otium.thread/#kill!). Its cleanups
run, but `try` cannot catch the cancellation. Joining it returns the kill
reason, which defaults to `:killed`.

For related tasks that should finish together, use
[`group`](/reference/otium.thread/#group) with `with`, and start children with
[`spawn-in`](/reference/otium.thread/#spawn-in). Leaving the group waits for its
children. By default, a child failure kills its siblings and interrupts the
enclosing body. The failure is reported when the group closes.

## Actors: messages and replies

[otium.actor](/reference/otium.actor/) adds request/reply calls and exit
monitoring to threads. Actors still start with `thread/spawn`.

[`send`](/reference/otium.thread/#send) queues a message without waiting for it
to be processed. [`receive`](/reference/otium.thread/#receive) takes the first
matching message and runs its clause, leaving unmatched messages queued. Its
patterns follow [Pattern matching](matching.md).

For a reply, use [`actor/call`](/reference/otium.actor/#call). A `:call` clause
receives the request and sends back its body's value.

This counter keeps its state in a loop argument:

```otium
(require otium.actor :as actor)

(def (serve count)
  (serve
    (thread/receive
      ({:op :add :n n} (+ count n))
      (:call 'get count)
      (:call 'fail (error "request failed"))
      ('crash (error "counter failed")))))

(def counter (thread/spawn serve 0))

(thread/send counter {:op :add :n 2})
(actor/call counter 'get)
; => 2
```

Each successful clause returns the count for the next iteration. Other threads
ask the counter to change its state rather than changing shared state
themselves.

`send` returns `#f` if the target has finished. A successful send does not
acknowledge that the message was handled.

## Request failures versus actor failures

An error inside a `:call` clause is sent back and raised in the caller. The
actor's `receive` waits for another message, so a failed request need not stop
the service:

```otium
(try
  (actor/call counter 'fail)
  (catch problem (condition-message problem)))
; => "request failed"

(actor/call counter 'get)
; => 2
```

A call waits five seconds by default; an optional third argument sets the
timeout in seconds. A timeout raises a `thread/timeout?` condition without
cancelling work already being handled.

An error escaping an ordinary message clause ends the actor just like any other
thread. To hear when an actor ends, use
[`monitor`](/reference/otium.actor/#monitor). It returns a reference identifying
the exit notification:

```otium
(def watch (actor/monitor counter))
(thread/send counter 'crash)

(thread/receive
  ({:exit _ :ref (== watch) :reason reason}
    (condition-message reason)))
; => "counter failed"
```

The actor also prints an error diagnostic. The exit message's reason is the
original condition for failure, `:normal` for normal completion, or the kill
reason for cancellation. Monitoring reports the exit; it does not restart the
actor.

A pending `actor/call` does not automatically turn an actor's exit into a
failure reply. Without a reply, it waits until its timeout. Use monitors when
you need to track the service's lifetime.

## Choosing threads or actors

Use threads for independent tasks whose results you will join, with a group when
those tasks should succeed or fail together.

Use actors for ongoing services with state and a message-based interface. Calls
provide answers; monitors report exits. Neither choice makes CPU-heavy work run
across more cores.

<!-- generated from concurrency.md.spec sha256:1cfe3155072c00e0f54e30a9880102eeffd2b67aa0f29d28c9724408570d5d5c -->
