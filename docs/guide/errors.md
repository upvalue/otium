# ✨ Errors and recovery

When a function cannot complete its work, it needs a way to report what went
wrong, release anything it acquired along the way, and let its caller decide
what to do next.

In Otium, errors are carried by values called **conditions**. For the common
case, [`try`](/reference/otium.core/#try) unwinds the call stack and catches a
condition higher up, and [`defer`](/reference/otium.core/#defer) (also written
[`unwind-protect`](/reference/otium.core/#unwind-protect)) runs cleanup forms on
the way out. When unwinding to a caller would throw away useful work in
progress, Otium also supports **restarts** and signal-site **handlers**. Like
Common Lisp's condition system rather than Scheme's `guard`, a handler bound
with [`handler-bind`](/reference/otium.core/#handler-bind) runs where the error
is signalled before the stack unwinds, so it can resume execution at a
[`restart-case`](/reference/otium.core/#restart-case) deep inside the call tree.

## Conditions and signalling errors

A condition is an ordinary value that describes a problem. By convention, it is
a [table](data-structures.md) with a `:type` symbol and a `:message` string.

Call [`error`](/reference/otium.core/#error) with a message string to build and
signal an `'error` condition. Any extra arguments after the string are stored as
an array under `:data`:

```otium
(def (check-port port)
  (unless (and (integer? port) (<= 1 port 65535))
    (error "port out of range" port))
  port)

(check-port 8080)
; => 8080
```

Calling `(check-port 70000)` signals the table
`{:type 'error :message "port out of range" :data [70000]}`. If nothing handles
or catches the condition, execution stops and the runtime reports the message.

## Catching conditions with `try`

Use [`try`](/reference/otium.core/#try) with one or more
`catch` clauses to intercept an error after the
stack unwinds to the `try` form. Each clause takes `(predicate name)`, tests the
unwinding condition with `predicate`, and runs the clause body of the first
match with the condition bound to `name`.

[`error?`](/reference/otium.core/#error?) matches any condition table that has a
`:type` entry. Read the message and type with
[`condition-message`](/reference/otium.core/#condition-message) and
[`condition-type`](/reference/otium.core/#condition-type), and read any extra
fields with ordinary table lookups:

```otium
(try
  (check-port 70000)
  (catch (error? c)
    (list (condition-type c)
          (condition-message c)
          (get c :data))))
; => (error "port out of range" [70000])
```

A bare `(catch c ...)` without a predicate matches any raised value, including
non-table values passed directly to `error`. If no clause matches, the condition
continues unwinding to outer `try` forms.

### Specific condition types

When callers need to tell one failure apart from another, pass `error` a table
with your own `:type` and domain keys. Register the type under `'error` with
[`define-condition`](/reference/otium.core/#define-condition) so general
`error` handlers still recognize it, and build a predicate for `catch` with
[`type-pred`](/reference/otium.core/#type-pred):

```otium
(define-condition 'config-error 'error)

(def (parse-port port)
  (unless (and (integer? port) (<= 1 port 65535))
    (error {:type 'config-error
            :message "port out of range"
            :port port}))
  port)

(try
  (parse-port -1)
  (catch ((type-pred 'config-error) c)
    (list (condition-message c) (get c :port)))
  (catch (error? c)
    (condition-message c)))
; => ("port out of range" -1)
```

Put specific `catch` clauses before general ones, since `try` chooses the first
clause whose predicate returns true.

## Cleaning up with `defer` and `unwind-protect`

When an error unwinds the stack, expressions waiting after the failing call are
skipped. [`defer`](/reference/otium.core/#defer) and
[`unwind-protect`](/reference/otium.core/#unwind-protect) are two names for the
same form:

```otium
(defer protected-expr
  cleanup ...)
```

It evaluates `protected-expr`, then runs the `cleanup` forms in order whether
`protected-expr` returns a value, raises an error, invokes a restart, or exits
the program. On a normal return, the form returns the value of `protected-expr`:

```otium
(let ((steps '()))
  (def (record! step)
    (set! steps (append steps (list step))))
  (list
    (defer
      (begin
        (record! :work)
        :ok)
      (record! :cleanup))
    steps))
; => (:ok (:work :cleanup))
```

When `protected-expr` signals an error, the cleanup forms run as the stack
unwinds through `defer`, before an enclosing `catch` clause runs:

```otium
(let ((steps '()))
  (def (record! step)
    (set! steps (append steps (list step))))
  (try
    (defer
      (begin
        (record! :work)
        (parse-port -1))
      (record! :cleanup))
    (catch (error? c)
      (record! :caught)
      steps)))
; => (:work :cleanup :caught)
```

For ports and other objects that implement `close`,
[`with`](/reference/otium.core/#with) wraps `unwind-protect` and closes the
binding automatically; see [Files and I/O](io.md). Use `defer` or
`unwind-protect` directly when your cleanup is an arbitrary action such as
resetting state or removing a temporary entry.

## Recovering at the error site with restarts

`try` only runs after the stack has unwound to it. That works when the whole
operation inside `try` should abort on failure, but it gets in the way when an
error happens partway through a larger job.

Suppose you are validating a list of ports with `map`. Low-level `parse-port`
knows how to detect a bad port, while the high-level caller knows whether to
abort the batch or substitute a default port. If you wrap `try` around `map`,
the first bad port unwinds out of `map` and discards the rest of the list.

Restarts split the mechanism of recovery from the policy:

1. The low-level function wraps the risky step in
   [`restart-case`](/reference/otium.core/#restart-case), naming one or more
   ways to continue.
2. The high-level caller wraps the batch in
   [`handler-bind`](/reference/otium.core/#handler-bind), pairing a condition
   predicate with a handler function.
3. When `error` is called, Otium runs matching handlers at the signal site
   before unwinding. A handler can call
   [`invoke-restart`](/reference/otium.core/#invoke-restart) to jump to a named
   clause in `restart-case`, or return normally to decline and let outer
   handlers or `try` take over.

Here, `validate-port` offers a `use-value` restart around `parse-port`:

```otium
(def (validate-port port)
  (restart-case
    (parse-port port)
    (use-value "Substitute a fallback port." (fallback)
      fallback)))
```

If the caller uses `try`, the restart goes unused. The error on `70000` unwinds
past `validate-port` and `map` to the `catch` clause:

```otium
(try
  (map validate-port '(80 70000 443))
  (catch (error? c)
    (list :aborted (get c :port))))
; => (:aborted 70000)
```

If the caller uses `handler-bind` instead, the handler runs while
`validate-port`
is still on the stack. Invoking `'use-value` with `8080` unwinds only to the
`restart-case` inside that single `validate-port` call, which returns `8080` to
`map` so the rest of the list is processed:

```otium
(handler-bind (((type-pred 'config-error)
                (fn (c) (invoke-restart 'use-value 8080))))
  (map validate-port '(80 70000 443)))
; => (80 8080 443)
```

Use `try` when the caller catching the error is also the place where execution
should resume. Reach for `restart-case` and `handler-bind` when a lower-level
function can keep going once a caller supplies a value, skips an item, or tells
it to retry.

<!-- generated from errors.md.spec sha256:d23f1008084d5ee8436dd235258f4659e3d6d5bc047e483aab7e669d4c6aec8b -->
