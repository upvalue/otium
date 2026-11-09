# ✨ Control flow

## Choose a value with if

Only `#f` and `nil` are falsy in Otium. Everything else is truthy, including
`0`, `""`, and the empty list `'()`.

Use `(if test then else)` to choose between two expressions. Only the chosen
branch runs, and its value becomes the result.

```otium
(let ((score 75))
  (if (>= score 60)
      :pass
      :fail))
; => :pass
```

You can omit the alternative. If the test is falsy, the result is `nil`:

```otium
(if #f 42)
; => nil
```

## Sequence expressions with begin

Each `if` branch takes one expression. Use `begin` when a branch needs several
steps. It evaluates them in order and returns the last value.

```otium
(let ((count 0))
  (if (< count 1)
      (begin
        (set! count (+ count 1))
        :updated)
      :unchanged))
; => :updated
```

An empty `(begin)` returns `nil`.

Function and `let` bodies already accept several expressions and return the last
value. You do not need `begin` just to sequence their bodies.

## Choose among several cases with cond

Use `cond` when you have several tests. Tests run in order; the first truthy
test selects its clause. The remaining clauses are skipped.

```otium
(let ((score 75))
  (cond
    ((>= score 90) :excellent)
    ((>= score 60) :pass)
    (else :fail)))
; => :pass
```

Put `else` last as the fallback. Without a matching clause, `cond` returns
`nil`.

Each clause can contain several body expressions; the last one's value is the
result. A clause containing only a test returns that test's value when it is
truthy.

## Run a body conditionally with when and unless

Use `when` for steps that should run only when a test is truthy. Use `unless`
when they should run only when the test is falsy. Both accept several body
expressions without `begin`.

```otium
(let ((count 0))
  (when (< count 1)
    (set! count (+ count 1))
    count))
; => 1
```

```otium
(let ((name nil))
  (unless name
    (set! name "anonymous"))
  name)
; => "anonymous"
```

When the body runs, its last value is the result. When it is skipped, the result
is `nil`.

## Combine tests and choose defaults with and and or

`and` and `or` evaluate left to right and short-circuit. They return operand
values, not necessarily booleans.

Use `and` to require several conditions. It stops at the first falsy value and
returns it. If every operand is truthy, it returns the last value.

```otium
(let ((score 75))
  (and (>= score 60) (< score 90)))
; => #t

(and 1 nil 3)
; => nil
```

Use `or` to choose the first truthy value, such as a supplied value or a
default. If all operands are falsy, it returns the last value.

```otium
(let ((name nil))
  (or name "anonymous"))
; => "anonymous"
```

Because empty strings and zero are truthy, `or` keeps them. It also treats `#f`
as a reason to use the fallback, so use an explicit `nil?` test if `#f` is a
valid supplied value.

With no operands, `(and)` returns `#t` and `(or)` returns `#f`.

## Repeat while mutable state meets a condition

Use `while` when changes to mutable state drive the loop. It checks its test
before each iteration, so the body may never run.

```otium
(let ((i 0)
      (total 0))
  (while (< i 5)
    (set! total (+ total i))
    (set! i (+ i 1)))
  total)
; => 10
```

The body accepts several expressions. Make sure it changes the state that lets
the loop finish. `while` itself returns `nil`; read the accumulated value
afterward.

For walking collections, building results, indexed repetition, and named `let`,
see [Loops](loops.md).

<!-- generated from control-flow.md.spec sha256:7c79108bd426d58655d37d9a3c8ca8c4b421834c2adb5229059d613b6e1c252b -->
