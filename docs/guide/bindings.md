# ✨ Bindings

Bindings name values so you can reuse them in expressions and functions.
Top-level bindings belong to a namespace; local bindings belong to an enclosing
scope, such as a function or `let`. When a local name matches an outer name, the
local binding takes precedence.

## Top-level definitions

Otium uses `def` in place of Scheme's `define`. Use it to name a value or define
a function:

```otium
(def unit-price "Price of one item." 12)

(def (total quantity)
  "Cost of quantity items."
  (* unit-price quantity))

(total 3) ; => 36
```

A value's optional docstring goes before its value expression. A function's
docstring goes after the parameter list, before the body.
[`describe`](/reference/otium.core/#describe) retrieves this documentation.

Function parameters are local bindings, and the function returns its last body
expression. See [Functions](functions.md) for more.

Evaluating a top-level `def` again replaces the existing value in the current
namespace. See [Namespaces](namespaces.md) for organizing definitions across
files.

## Local bindings with `let`

Use `let` for names needed only within a body. Put binding pairs in parentheses.
The body's last expression is its result.

Plain `let` is sequential, like Scheme's `let*`. Each initializer can use
bindings introduced before it:

```otium
(let ((quantity 3)
      (subtotal (total quantity))
      (shipping 5))
  (+ subtotal shipping)) ; => 41
```

Here, `subtotal` can use `quantity`. Otium has no separate `let*` form.

Inside a function or `let` body, `def` creates a local binding rather than a
namespace definition:

```otium
(def (discounted-total quantity)
  (def subtotal (total quantity))
  (- subtotal 2))

(discounted-total 3) ; => 34
```

Local definitions are in scope throughout their body, so local functions can
refer to one another regardless of definition order. Initializers still run in
order. Do not read a local value before its definition has run.

Named `let` adds a local function name for recursion. Unlike plain `let`, its
initializers all see the enclosing scope, not the new bindings. See
[Loops](loops.md) for examples.

## Changing a binding

Use `set!` to assign a new value to an existing binding. It changes the nearest
local binding, or the namespace binding if there is no local one. An unbound
name is an error.

```otium
(let ((unit-price 20))
  (set! unit-price (+ unit-price 2))
  unit-price) ; => 22

unit-price ; => 12

(set! unit-price 15)
(total 3) ; => 45
```

The local assignment leaves the top-level `unit-price` unchanged. The later
top-level assignment changes the value that `total` reads.

## Keeping state across reloads

Use `defonce` for top-level state that should survive reevaluating or reloading
a file. It defines a name only if the current namespace does not already define
it. Otherwise, it keeps the existing value and skips the initializer.

```otium
(defonce orders-filled 0)
(set! orders-filled (+ orders-filled 1))

;; Evaluating this definition again keeps the changed value.
(defonce orders-filled 0)
orders-filled ; => 1

;; Ordinary def replaces the value.
(def unit-price 12)
unit-price ; => 12
```

Use ordinary `def` for functions and values you want a reload to update, and
`defonce` for state you want to retain. A `defonce` binding is still mutable:
`set!` can change it.

See [Live programming](live-programming.md) for reloading code while keeping a
running program's state.

<!-- generated from bindings.md.spec sha256:2cc4f45c31981304eda1f15d2e46b97f80a11ff1295baf246a6172c92774517f -->
