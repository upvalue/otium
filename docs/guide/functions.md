# ✨ Functions

## Define and call a function

Use `fn` to make a function and `(def (name args) ...)` to define a named one.
Call either by putting it first in a list.

```otium
((fn (x) (* x x)) 5) ; => 25

(def (square x)
  (* x x))

(square 5) ; => 25
```

Arguments are evaluated left to right. A function evaluates its body expressions
in order and returns the last one's value.

Functions are values, so you can pass them to other functions:

```otium
(map square [2 3 4]) ; => [4 9 16]
```

## Add optional arguments

Put optional positional parameters after `&optional`. Each needs a
`(name default)` pair.

```otium
(def (defaults x &optional (y 10) (z (+ x y)))
  (list x y z))

(defaults 2)     ; => (2 10 12)
(defaults 2 3)   ; => (2 3 5)
(defaults 2 3 4) ; => (2 3 4)
```

Defaults run only when an argument is omitted, from left to right. They can use
preceding parameters and enclosing lexical bindings. Passing `nil` or `#f`
supplies a value; it does not request the default.

```otium
((fn (&optional (x 7)) x) nil) ; => nil
```

The same parameter syntax works with `fn`. Too few required arguments or too
many arguments is an error.

## Accept a variable number of arguments

A rest parameter collects the remaining arguments into a list:

```otium
(def (gather first . more)
  (list first more))

(gather 1)     ; => (1 ())
(gather 1 2 3) ; => (1 (2 3))
```

You can also write `(first & more)`. To collect every argument, give `fn` a
single parameter name without parentheses:

```otium
((fn args args) 1 2) ; => (1 2)
```

Optional and rest parameters cannot be combined.

Runtime rest parameters trigger the `rest-arguments` lint warning. For an
intentionally variadic API, suppress it with a reason as described in [Lint and
diagnostics](lint.md).

## Capture local bindings

A function keeps access to the lexical bindings where it was created, even after
the enclosing call returns:

```otium
(def (make-adder n)
  (fn (x) (+ n x)))

(def add-ten (make-adder 10))

(add-ten 5) ; => 15
```

Captured bindings can also hold state. Use `set!` to change them:

```otium
(def (make-counter)
  (let ((n 0))
    (fn ()
      (set! n (+ n 1))
      n)))

(def counter (make-counter))

(counter) ; => 1
(counter) ; => 2
```

Each call to `make-counter` creates a separate `n`.

## Recur with tail calls

When a function returns another call's result directly, that call is in tail
position. Otium runs tail calls without growing the call stack.

```otium
(def (count-down n &optional (total 0))
  (if (= n 0)
      total
      (count-down (- n 1) (+ total 1))))

(count-down 1000) ; => 1000
```

The recursive call is the result of the `if` branch, and the `if` is the
function's last expression. There is no work left after that call.

Keep recursive calls in tail position for long-running recursion. A call inside
`(+ 1 (recur ...))` is not a tail call because the addition must still run. See
[Loops](loops.md) for named `let` and other loop forms.

## Call with collected arguments

Use `apply` when arguments are already in a list or array. It spreads its final
argument into the call; any arguments before it are passed first.

```otium
(apply + '(1 2 3))     ; => 6
(apply + 1 2 '(3 4))   ; => 10
(apply max [3 9 2])    ; => 9
```

## Call collections and keywords

Tables and arrays can be called for lookup. Arrays use zero-based indexes. A
keyword can be called with a collection to look up that key.

```otium
(def settings {:size 12})
(def items [3 4])

(settings :size)     ; => 12
(:size settings)     ; => 12
(items 1)            ; => 4
(settings :absent 8) ; => 8
```

These calls behave like `get`: a missing entry returns `nil` unless you supply a
default.

Callable values also work with `apply` and as callbacks:

```otium
(apply settings '(:size))         ; => 12
(map :size [{:size 12} {:size 8}]) ; => [12 8]
```

## Thread values and combine functions

Load the helpers in `otium.extra` explicitly:

```otium
(require otium.extra :as extra)
```

### Thread a value through calls

`extra/->` inserts the value as the first argument of each form:

```otium
(extra/-> 5
  (+ 1)
  (* 2)) ; => 12
```

This is equivalent to `(* (+ 5 1) 2)`.

Use `extra/->>` when the sequence or value belongs in the last argument
position:

```otium
(extra/->> [1 2 3 4]
  (filter even?)
  (map square)
  (reduce + 0)) ; => 20
```

A bare function name in either threading macro is called with the threaded value
as its only argument.

### Fix arguments with partial

`extra/partial` returns a function that prepends the supplied arguments to its
later arguments.

```otium
(def add-three (extra/partial + 1 2))

(add-three 3) ; => 6
```

### Compose functions

`extra/comp` combines functions from right to left. The rightmost function
receives the call's arguments; each remaining function receives the previous
result as one argument.

```otium
(def next-text (extra/comp str inc))

(next-text 4) ; => "5"
```

<!-- generated from functions.md.spec sha256:43943b7e404a43ae204901935c9c1abf9752c6a911102b3d3937f11b9c9117d4 -->
