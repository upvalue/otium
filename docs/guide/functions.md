# ✨ Functions

Functions let you name reusable work and pass behavior to operations such as
`map`. They are values, so you can bind them to names, pass them as arguments,
and return them from other functions.

## Define and call functions

Use `def`, rather than Scheme's `define`, to define a named function. Use `fn`
to make an anonymous function.

```otium
(def (square x)
  (* x x))

(square 6) ; => 36

(map (fn (x) (* x x)) [1 2 3]) ; => [1 4 9]
```

A call puts the function before its arguments. Arguments are evaluated before
the function runs. The body evaluates its expressions in order and returns the
last expression's value.

## Optional arguments

Put required parameters first, followed by `&optional` and the optional
parameters. Each optional parameter must be a `(name default)` pair.

```otium
(def (span start &optional (width 10) (end (+ start width)))
  (list start end))

(span 5)      ; => (5 15)
(span 5 3)    ; => (5 8)
(span 5 3 20) ; => (5 20)
```

These arguments are positional. Defaults run from left to right, only for
omitted arguments. They can use preceding parameters and enclosing bindings.

Passing `nil` or `#f` supplies a value; it does not request the default.

```otium
((fn (&optional (value 7)) value) nil) ; => nil
```

Passing too few required arguments or too many arguments is an error.

## Rest arguments

A rest parameter gathers any remaining arguments into a list. Scheme's dotted
syntax works:

```otium
(def (gather first . more)
  (cons first more))

(gather 1)     ; => (1)
(gather 1 2 3) ; => (1 2 3)
```

You can also write the parameter list as `(first & more)`. A bare symbol after
`fn` gathers all arguments:

```otium
((fn args args) 1 2 3) ; => (1 2 3)
```

Optional and rest parameters cannot be combined in one parameter list.

The linter warns about runtime rest parameters. For an intentionally variadic
API, see [Lint and diagnostics](lint.md) for how to suppress the warning with a
reason.

## Closures

A function keeps access to the lexical bindings surrounding its definition. You
can use this to return a function configured with values from an earlier call.

```otium
(def (make-adder amount)
  (fn (x) (+ x amount)))

(def add-ten (make-adder 10))

(add-ten 5) ; => 15
```

Here `add-ten` retains access to `amount` after `make-adder` returns.

## Tail calls

A call is in tail position when its result is returned directly, with no work
left afterward. Otium guarantees that tail calls do not grow the stack,
including calls between different functions.

```otium
(def (sum-to n &optional (total 0))
  (if (= n 0)
      total
      (sum-to (- n 1) (+ total n))))

(sum-to 100000) ; => 5000050000
```

The recursive call supplies the result of the `if` branch, and that `if` is the
function's last expression. The addition happens before the call. Wrapping the
recursive call in an addition would leave work afterward, so it would no longer
be tail-recursive.

## Call with collected arguments

Use [`apply`](/reference/otium.core/#apply) when arguments are already in a list
or array. Its final argument supplies the collected arguments; any arguments
before it are passed first.

```otium
(apply + '(1 2 3))   ; => 6
(apply + 10 [1 2 3]) ; => 16
```

## Call collections and keywords

Tables, arrays, and keywords can also appear in the function position. A table
call looks up a key, and an array call looks up a zero-based index. A keyword
call looks up that keyword in the supplied collection.

These calls follow [`get`](/reference/otium.core/#get): a missing entry returns
`nil` unless you supply a default.

```otium
(def person {:name "Ada"})
(def names ["Ada" "Grace"])

(person :name)    ; => "Ada"
(:name person)   ; => "Ada"
(names 1)        ; => "Grace"
(person :age)    ; => nil
(:age person 0) ; => 0
```

Callable keywords are useful as lookup callbacks:

```otium
(map :name [{:name "Ada"} {:name "Grace"}])
; => ["Ada" "Grace"]
```

## Build calls with helpers

[otium.extra](/reference/otium.extra/) provides helpers for arranging calls and
building functions. Load it explicitly.

### Thread values through calls

The [`->`](/reference/otium.extra/#->) macro inserts a value as the first
argument of each successive call. [`->>`](/reference/otium.extra/#->>) inserts
it as the last argument.

```otium
(require otium.extra :as extra)

(extra/-> {:score 4}
  (get :score)
  (+ 1))
; => 5

(extra/->> [1 2 3]
  (map square)
  (reduce + 0))
; => 14
```

Choose the direction based on where each operation expects the value being
passed along.

### Fix arguments and compose functions

[`partial`](/reference/otium.extra/#partial) returns a function with leading
arguments already supplied.

```otium
(def add-five (extra/partial + 5))

(add-five 3) ; => 8
```

[`comp`](/reference/otium.extra/#comp) combines functions from right to left.
The rightmost function receives the call's arguments; each remaining function
receives the preceding result.

```otium
(def square-after-add (extra/comp square +))

(square-after-add 2 3) ; => 25
```

<!-- generated from functions.md.spec sha256:5077a4c1e6491cd5e700a2ac4207a6ecbbe30375e6b6b97f3e716dd27e4fd25e -->
