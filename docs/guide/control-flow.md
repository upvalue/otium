# ✨ Control flow

Branching chooses which expressions to evaluate; sequencing runs several
expressions in order. Otium's conditionals are expressions too, so you can use
their results as values.

Use `if` for two alternatives, `cond` for several cases, and `when` or `unless`
for a conditional action. Use `and` and `or` to combine tests or select values
without evaluating unnecessary expressions.

## Truth and falsehood

Only `nil` and `#f` are false. Every other value is truthy, including zero,
empty strings, and the empty list. In particular, `'()` is distinct from `nil`.

```otium
(if 0 :yes :no)   ; => :yes
(if '() :yes :no) ; => :yes
(if "" :yes :no)  ; => :yes
(if #f :yes)      ; => nil
```

## Choose between alternatives with `if`

`if` evaluates its test, then evaluates only the selected branch and returns
that branch's value. An `if` without an else returns `nil` when its test is
false.

```otium
(let ((score 75))
  (if (>= score 60)
      :pass
      :fail))
; => :pass
```

## Choose among cases with `cond`

`cond` tests clauses from top to bottom. It selects the first clause with a
truthy test and skips later tests and bodies. Put an `else` clause last for a
fallback.

```otium
(let ((score 75))
  (cond
    ((>= score 90) :excellent)
    ((>= score 60) :pass)
    (else :retry)))
; => :pass
```

A clause can contain several body expressions. They run in order, and `cond`
returns the last value. A clause containing only a test returns that test's
value when truthy.

If no clause matches, `cond` returns `nil`:

```otium
(cond
  ((> 2 3) :yes))
; => nil
```

## Group expressions with `begin`

Each branch of `if` accepts one expression. Use `begin` when a branch needs
several steps. It evaluates them in order and returns the last value. An empty
`begin` returns `nil`.

Here, `set!` changes an existing binding before the branch constructs its
result:

```otium
(let ((count 0))
  (if (= count 0)
      (begin
        (set! count (+ count 1))
        (list :started count))
      :already-started))
; => (:started 1)
```

Bodies of `let`, functions, and `cond` clauses already accept multiple
expressions, so they do not need an extra `begin`.

## Run conditional actions with `when` and `unless`

Use [`when`](/reference/otium.core/#when) for an action that should run on a
truthy test, and [`unless`](/reference/otium.core/#unless) for one that should
run on a false test.

Both accept multiple body expressions, evaluate them in order, and return the
last value. If the body is skipped, they return `nil`.

```otium
(let ((count 0))
  (when (= count 0)
    (set! count (+ count 1))
    count))
; => 1

(unless #f :ready) ; => :ready
(when #f :ready)   ; => nil
(unless #t :ready) ; => nil
```

## Stop early with `and` and `or`

Both forms evaluate left to right and return actual values, not necessarily
booleans.

- `and` stops at the first false value and returns it. Otherwise, it returns
  the last value.
- `or` stops at the first truthy value and returns it. Otherwise, it returns
  the last value.

Use `and` to guard an expression and `or` to choose a fallback.

```otium
(and (> 3 2) :ready)             ; => :ready
(and nil (error "not reached"))  ; => nil

(or nil #f :fallback)            ; => :fallback
(or 0 (error "not reached"))     ; => 0
(or #f nil)                      ; => nil
```

A fallback with `or` replaces both `nil` and `#f`. It does not replace zero or
an empty string.

With no arguments, `and` returns `#t` and `or` returns `#f`.

## Repeat while state changes

Use `while` when repetition depends on mutable state. It checks its test before
every iteration and runs its body in order while the test remains truthy. The
body may run zero times.

```otium
(let ((remaining 3)
      (total 0))
  (while (> remaining 0)
    (set! total (+ total remaining))
    (set! remaining (- remaining 1)))
  total)
; => 6
```

`while` itself always returns `nil`, so the example reads `total` afterward to
get the accumulated result. Make sure something changes so the test can
eventually become false.

For walking collections, building results, or using named loops, see
[Loops](loops.md).

<!-- generated from control-flow.md.spec sha256:fb1ff3282e0f3d4e4f18bf032d0b6754446ba2362dfa5541ee17c34510bb3a7b -->
