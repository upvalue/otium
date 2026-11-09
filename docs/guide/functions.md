# ✨ Functions

Otium functions are values. You can bind reusable work to a name, pass it as an
argument to other code, such as a sequence transformation, or return it from
another function.

## Defining and calling functions

Use `def`, rather than Scheme's `define`, to define a named function, and `fn`
to create an anonymous function.

```otium
(def (square x)
  (* x x))

(square 6)                    ; => 36
((fn (x) (* x x)) 6)          ; => 36
(map square [2 3 4])          ; => [4 9 16]
```

The function form of `def` is shorthand for binding an `fn` to a name. A
function body can contain several expressions. They run in order, and the last
expression supplies the result; there is no explicit return statement.

In an ordinary call, Otium evaluates the function expression, then its arguments
from left to right.

## Optional arguments

Callers may leave out optional positional arguments. After `&optional`, write
each parameter as a `(name default)` pair. This extends Scheme's usual
parameter-list syntax.

```otium
(def (window start &optional (width 10) (end (+ start width)))
  (list start width end))

(window 5)        ; => (5 10 15)
(window 5 3)      ; => (5 3 8)
(window 5 3 20)   ; => (5 3 20)
```

Defaults run at call time, only for omitted arguments. They run left to right
and can use preceding parameters and enclosing lexical bindings. Passing `nil`
or `#f` supplies a value; it does not request the default.

Required parameters go before `&optional`. The same syntax works in `fn`.
Calling with too few required arguments or too many arguments is an error.

## Rest arguments

A rest parameter collects any remaining arguments into a list. Scheme's dotted
syntax works:

```otium
(def (with-extras first . more)
  (list first more))

(with-extras 1)       ; => (1 ())
(with-extras 1 2 3)   ; => (1 (2 3))
```

You can also write the parameter list as `(first & more)`. To collect all
arguments, give `fn` a single parameter name instead of a parameter list:

```otium
((fn args args) 1 2 3)   ; => (1 2 3)
```

Optional and rest parameters cannot be combined in one function.

Runtime rest parameters trigger a lint warning. For a genuinely variadic API,
you can suppress the warning with a reason; see [Lint and diagnostics](lint.md).

## Closures

A closure retains access to the lexical bindings where it was created, even
after the enclosing function returns.

```otium
(def (make-adder amount)
  (fn (x) (+ x amount)))

(def add-ten (make-adder 10))

(add-ten 5)   ; => 15
```

Each call to `make-adder` creates a function with its own captured `amount`.

## Tail calls

As in Scheme, tail calls do not grow the stack. A call is in tail position when
its result becomes the enclosing function's result without further work.

```otium
(def (sum-to n &optional (total 0))
  (if (= n 0)
      total
      (sum-to (- n 1) (+ total n))))

(sum-to 100000)   ; => 5000050000
```

The recursive call is the final expression in an `if` branch, and the `if` is
the function's final expression. Keeping a running total avoids addition after
the recursive call returns.

Tail calls between different functions have the same guarantee. Ordinary
recursion with work remaining after the call still uses stack space.

## Calling with collected arguments

Use [`apply`](/reference/otium.core/#apply) when arguments are already in a list
or array. It spreads its final argument into individual arguments, passing any
arguments before it first.

```otium
(apply + '(1 2 3))      ; => 6
(apply + 10 [1 2 3])    ; => 16
```

## Callable collections and keywords

You can also call tables, arrays, and keywords in Otium. These calls perform
lookups, like [`get`](/reference/otium.core/#get).

A table takes a key, and an array takes a zero-based index. A keyword takes the
collection to look itself up in. A missing entry returns `nil` unless you supply
a default.

```otium
(def settings {:size 12})
(def sizes [12 18 24])

(settings :size)          ; => 12
(sizes 1)                 ; => 18
(:size settings)          ; => 12
(settings :missing)       ; => nil
(:missing settings 20)    ; => 20

(map :size [{:size 12} {:size 18}])   ; => [12 18]
```

You can pass callable values to higher-order functions such as `map`, or call
them through `apply`.

## Combining functions with `otium.extra`

Load `otium.extra` explicitly to use its helpers for arranging calls or building
functions.

The threading macros `->` and
`->>` make nested calls read in execution order.
`->` inserts the preceding value as the first argument of each form; `->>`
inserts it as the last.

Using `square` from above:

```otium
(require otium.extra :as extra)

(extra/-> 5
  (+ 1)
  (* 2))                       ; => 12

(extra/->> [1 2 3]
  (map square)
  (reduce + 0))                ; => 14
```

`partial` creates a function with leading
arguments already supplied. `comp` composes
functions right to left: the rightmost function receives the call's arguments,
and each remaining function receives the preceding result.

```otium
(def add-five (extra/partial + 5))
(add-five 7)                   ; => 12

(def square-after-add (extra/comp square +))
(square-after-add 2 3)          ; => 25
```

<!-- generated from functions.md.spec sha256:5077a4c1e6491cd5e700a2ac4207a6ecbbe30375e6b6b97f3e716dd27e4fd25e -->
