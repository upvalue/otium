# ✨ Errors and recovery

Error handling lets a function report a failure and leave the response to its
caller. The caller can abandon the operation, return a fallback, or choose a
recovery offered by the failing code.

Otium works like Common Lisp's condition system rather than Scheme's `guard`. It
separates reporting a problem from transferring control:

- A condition describes the problem, conventionally as a table with `:type` and
  `:message`.
- A handler receives a condition while the failing computation is still on the
  stack.
- A restart offers a named way to recover near the failure.

Use `try` when you want to stop an operation and handle its failure.

## Signal and catch errors

[`error`](/reference/otium.core/#error) signals a condition. If no handler
chooses a recovery, it unwinds the stack: execution leaves the failing calls and
runs their cleanup forms until it reaches a matching `catch`.

Passing a string to `error` creates an error condition with that message. Use
[`condition-message`](/reference/otium.core/#condition-message) to read it:

```otium
(try
  (error "Price is missing")
  (catch (error? problem)
    (condition-message problem)))
; => "Price is missing"
```

A `catch` clause contains a predicate and a name for the condition. Here,
`error?` recognizes condition tables. The first matching clause supplies the
value of the whole `try`; if none matches, the error continues outward. On
success, `try` returns its body's value.

You can also pass a condition table directly to `error` when you need a specific
type or extra information.

[`signal`](/reference/otium.core/#signal) notifies handlers without requiring
the operation to stop. If every handler returns normally, `signal` returns
`nil`. In contrast, `error` never returns normally. Use `error` when continuing
without recovery would be invalid.

## Clean up on every exit

[`defer`](/reference/otium.core/#defer) evaluates one protected expression, then
runs its cleanup forms whether that expression succeeds or leaves through an
error or restart. [`unwind-protect`](/reference/otium.core/#unwind-protect) is
another name for the same form.

Wrap several protected expressions in `begin`. Unlike a scope-exit declaration
in some languages, `defer` surrounds the work it protects:

```otium
(let ((events []))
  (let ((result
          (try
            (defer
              (begin
                (push! events 'working)
                (error "Cannot finish"))
              (push! events 'cleaned))
            (catch (error? problem)
              (push! events 'caught)
              (condition-message problem)))))
    (list result events)))
; => ("Cannot finish" [working cleaned caught])
```

Cleanup runs before the catch body. On normal return, the protected expression's
value is preserved; cleanup values are ignored. Nested cleanups run from the
inside out.

Use this pattern to close ports or release resources, placing it immediately
around the work that uses them. Cleanup also runs for Otium's interrupts,
`quit`, and `exit`, which `try` cannot catch. Keep cleanup reliable: an error
raised during cleanup replaces the pending result or error.

## Recover near the failure

Use restarts when abandoning the whole operation would discard useful work. The
failing function knows which repairs are safe; its caller knows which repair
fits the situation.

[`restart-case`](/reference/otium.core/#restart-case) establishes named recovery
choices around an expression.
[`handler-bind`](/reference/otium.core/#handler-bind) installs handlers that run
before unwinding, including for errors inside called functions.

In this price lookup, the caller can supply a replacement value for a missing
price. It chooses zero and continues calculating the total:

```otium
(define-condition 'missing-price 'error)

(def (price item)
  (restart-case
    (let ((value (get item :price)))
      (if (nil? value)
          (error {:type 'missing-price
                  :message "Price is missing"})
          value))
    (use-value (value) value)))

(def (total items)
  (reduce (fn (sum item) (+ sum (price item))) 0 items))

(def items [{:price 10} {} {:price 5}])

(handler-bind
    (((type-pred 'missing-price)
      (fn (problem)
        (invoke-restart 'use-value 0))))
  (total items))
; => 15

(try
  (total items)
  (catch ((type-pred 'missing-price) problem)
    0))
; => 0
```

`define-condition` registers the condition type; `type-pred` builds a predicate
matching that type and its subtypes.

The handler invokes `use-value` while the restart inside `price` is still
available. [`invoke-restart`](/reference/otium.core/#invoke-restart) transfers
control to that restart's clause, running intervening cleanups. The clause's
result becomes the value of the whole `restart-case`, so `price` returns zero
and `total` continues.

The `try` version instead abandons `total`. Its fallback replaces the entire
total, not the missing price. By the time its catch body runs, the restart
inside `price` is gone.

Returning a value from a handler does not supply a replacement. A normal return
declines the condition and lets other handlers try. To choose a recovery, invoke
a restart.

Prefer `try` when failure should end an operation. Add restarts when callers
need to choose a local repair and let the surrounding computation continue.

<!-- generated from errors.md.spec sha256:d23f1008084d5ee8436dd235258f4659e3d6d5bc047e483aab7e669d4c6aec8b -->
