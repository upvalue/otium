# ✨ Errors and recovery

## Signal an error

Call `error` when a computation cannot continue:

```otium
(error "Missing price")
```

A string argument becomes a condition table with `:type 'error` and a
`:message`. Extra arguments are stored in its `:data` array. You can also pass a
condition table directly.

`error` does not return normally. It gives active handlers a chance to recover,
then unwinds if none does.

Use `signal` only when continuing without recovery is acceptable. It calls
handlers in the same way, but returns `nil` if they all decline.

## Catch an error

Use `try` to abandon a failed computation and return another value:

```otium
(try
  (error "Missing price")
  (catch (error? problem)
    (condition-message problem)))
;; => "Missing price"
```

The predicate in `(catch (error? problem) ...)` decides whether the clause
matches. The condition is then bound to `problem`. Use `condition-message` to
read its message; it returns `nil` when there is none.

Put catch clauses after the body forms. Otium tries them in order and uses the
first matching clause's result as the value of `try`. If no clause matches, the
error continues outward. On success, `try` returns its body's last value.

`error` can also raise a non-string value unchanged. To catch any raised value,
use `(catch problem ...)` without a predicate.

## Run cleanup

Wrap an expression in `defer` when cleanup must run whether the expression
succeeds or fails:

```otium
(let ((cleaned? #f))
  (let ((message
          (try
            (defer
              (error "Work failed")
              (set! cleaned? #t))
            (catch (error? problem)
              (condition-message problem)))))
    (list message cleaned?)))
;; => ("Work failed" #t)
```

The first expression is the protected work. The remaining expressions are
cleanup, run in order. Use `begin` if the protected work needs several
expressions.

`unwind-protect` is another name for the same form:

```otium
(let ((cleaned? #f))
  (let ((value
          (unwind-protect
            7
            (set! cleaned? #t))))
    (list value cleaned?)))
;; => (7 #t)
```

Cleanup does not change the protected expression's result unless it raises an
error itself. A cleanup error replaces the original result or error, so take
care not to hide the failure you meant to report.

Nested cleanups run from the inside out. They also run when control transfers to
a restart or leaves through `quit`, `exit`, or an interrupt. Those last three
transfers cannot be caught by `try`.

## Recover where the error happens

Use a restart when a caller can supply a recovery choice and the surrounding
computation should continue.

Suppose a missing price can be replaced with a value chosen by the caller:

```otium
(def (price-or-recover price)
  (if (nil? price)
      (restart-case (error "Missing price")
        (use-value (replacement) replacement))
      price))

(handler-bind
  ((error? (fn (problem)
             (invoke-restart 'use-value 12))))
  (+ 5 (price-or-recover nil)))
;; => 17
```

`restart-case` makes `use-value` available while its protected expression runs.
`handler-bind` installs a handler for errors raised anywhere inside its body,
including functions it calls.

The handler runs before the stack unwinds. It invokes `use-value` with `12`,
transferring control to that restart clause. The clause returns `12` as the
value of `restart-case`, so `price-or-recover` returns `12` and the addition
continues.

With an outer `try`, the addition is abandoned:

```otium
(try
  (+ 5 (price-or-recover nil))
  (catch (error? problem)
    12))
;; => 12
```

By the time the catch clause runs, control has already left `price-or-recover`
and its restart is no longer available. Use `try` when replacing the whole
failed computation is appropriate. Use a handler and restart when the caller
should choose a recovery value while preserving the work around that recovery
point.

Returning a value from a handler does not recover from an error. A normal return
declines the condition, and Otium continues looking for a handler. Call
`invoke-restart` to select the recovery.

Place `restart-case` around the expression whose result you want to replace.
Invoking a restart leaves that protected expression, runs any intervening
cleanup, and uses the restart clause's result as the value of the whole
`restart-case`.

<!-- generated from errors.md.spec sha256:6067167be7fc24f0dce36692189f0c646bc6deae01ffa480562880bbf7f2cafb -->
