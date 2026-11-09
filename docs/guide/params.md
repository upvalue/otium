# ✨ Dynamic params

Use a param for a setting that several functions need during an operation,
without passing it through every intervening call. A temporary binding applies
to calls made while that binding is active.

## Define and read a setting

Define params at the top level with `defparam`, giving each a default value:

```otium
(defparam detail-level :brief)

(detail-level) ; => :brief
```

Read a param by calling it with no arguments: `(detail-level)`. The name
`detail-level` alone refers to the param itself.

## Override it for a call

Use `with-params` to bind a temporary value:

```otium
(def (report-detail)
  (detail-level))

(with-params ((detail-level :full))
  (report-detail)) ; => :full

(report-detail) ; => :brief
```

`report-detail` sees the override even though it was defined outside
`with-params`. The value depends on the active calls, not where the function was
defined. Functions called further down see the same override.

`with-params` returns the value of its last body expression. When it exits, the
previous setting is restored, including when an error unwinds out of the body.

## Nest overrides

The innermost active binding wins. Leaving it restores the enclosing value:

```otium
(with-params ((detail-level :full))
  (list
    (detail-level)
    (with-params ((detail-level :brief))
      (detail-level))
    (detail-level)))
; => (:full :brief :full)

(detail-level) ; => :brief
```

You can bind several params in one `with-params`. Bindings take effect in order,
so later value expressions can read earlier bindings.

## Choose between a param and an argument

Prefer an argument for a function's main inputs, or when callers need to see and
choose the value at each call.

Use a param for a shared setting with a useful default, such as reporting detail
or an output destination. It helps when intermediate functions would otherwise
accept an argument only to pass it onward.

Keep overrides around the operation that needs them. A param makes a dependency
less visible in a function's argument list, so use ordinary arguments when that
would make the code easier to follow.

<!-- generated from params.md.spec sha256:70b89bf9476913ef70565c485c3c8d078850dfabd7429c0742162a2faea4c51e -->
