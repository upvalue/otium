# ✨ Dynamic params

Some settings should apply throughout an operation, including helper calls,
without being passed through every function. A param gives a setting a default
value and lets a caller override it for a stretch of code.

Otium params work like Scheme's parameters and `parameterize`: `defparam`
defines a setting, `(setting)` reads its current value, and `with-params`
temporarily overrides it.

## Choosing a param

Use an argument for a function's ordinary inputs, especially when callers need
to see and choose them explicitly.

A param suits a setting shared across a chain of calls, such as diagnostic
verbosity or output formatting. Intermediate functions need not accept and
forward a setting they do not use, though this makes the dependency less visible
in function signatures.

An ordinary global is enough for a value that does not need scoped overrides. If
you find yourself changing a global before an operation and restoring it
afterward, a param usually expresses that intent more safely.

## Define and read a setting

Define a param at the top level with
[`defparam`](/reference/otium.core/#defparam), giving it a name and default
value:

```otium
(defparam verbose? #f)

(verbose?)
;; => #f
```

The name `verbose?` refers to the param itself. Calling it with no arguments
reads its current value.

## Override it for an operation

[`with-params`](/reference/otium.core/#with-params) takes parenthesized pairs of
params and temporary values, followed by a body. It returns the body's last
result.

Continuing with `verbose?`:

```otium
(def (status-label name)
  (if (verbose?) (list :status name :ready) name))

(status-label "worker")
;; => "worker"

(with-params ((verbose? #t))
  (status-label "worker"))
;; => (:status "worker" :ready)

(verbose?)
;; => #f
```

The override is dynamically scoped: it applies to calls made while the body
runs, including calls further down the chain. `status-label` sees the override
even though it was defined outside `with-params`.

Here, `name` is an argument because it identifies what the function is
describing. Verbosity is a param because the caller may want one setting to
govern many such calls.

Otium restores the previous value automatically when the body finishes,
including when an error exits the body.

## Nest overrides

The innermost active binding wins. Leaving an inner `with-params` restores the
enclosing value:

```otium
(with-params ((verbose? #t))
  (list (verbose?)
        (with-params ((verbose? #f)) (verbose?))
        (verbose?)))
;; => (#t #f #t)

(verbose?)
;; => #f
```

Neither override changes the default, so outside both scopes the setting is back
to `#f`.

<!-- generated from params.md.spec sha256:6bdad7c02541daee6d6f836c955d1df93082fcda59505e5847def15d57905d0f -->
