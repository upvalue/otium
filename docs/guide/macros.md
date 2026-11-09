# ✨ Macros

## Define a macro

Use `defmacro` to receive unevaluated argument forms and return the code that
should replace the call. Define the macro before any top-level form that uses
it.

```otium
(defmacro twice (expression)
  #`(let ((value# ,expression))
      (+ value# value#)))

(twice (+ 2 3)) ; => 10
```

Here `expression` receives the list `(+ 2 3)`, not the number `5`. The macro
runs during expansion. Its returned `let` runs later and evaluates the supplied
expression once.

A macro body can use ordinary Otium functions that are already defined.

## Build code with templates

Use syntax quote, ``#` ``, for code templates:

- `,expression` inserts the value of an expression into the template.
- `,@expressions` splices a list of forms into the surrounding list.

A rest parameter collects unevaluated forms into a list:

```otium
(defmacro listed (& forms)
  #`(list ,@forms))

(listed (+ 1 2) (* 3 4)) ; => (3 12)
```

The comma expressions run while the macro builds its result. The inserted forms
run when the resulting code is evaluated.

Keep plain backquote for data. It supports comma and comma-at, but does not
qualify names or make `name#` fresh:

```otium
(let ((n 3))
  `(amount ,n)) ; => (amount 3)
```

## Keep names safe

Syntax quote resolves ordinary names in the namespace where the template is
written. Core and referred names use their defining namespace; aliases expand to
full namespace names. Unresolved bare names use the template's current
namespace.

```otium
(ns app.tools)

(def- (helper x)
  (* x 10))

(defmacro tenfold (expression)
  #`(helper ,expression))

(tenfold 4) ; => 40
```

The generated call refers to `app.tools/helper`. It still reaches that private
helper when another namespace uses `tenfold`, even if the caller has a local
variable named `helper`. Special forms such as `let` stay bare, as do symbols
inside quoted data.

### Introduce fresh bindings

Give each binding your template introduces a name ending in `#`, as with
`value#` in `twice`. Every occurrence of that name in one template denotes the
same fresh symbol. Each evaluation of the template creates a new one.

```otium
(let ((value 7))
  (twice value)) ; => 14
```

The generated binding cannot capture the caller's `value`. A bare binding name
in a syntax-quoted template becomes qualified, and binding a qualified name is
an error.

### Capture a name deliberately

Use `,'name` to insert a bare symbol that should resolve in the caller's scope:

```otium
(defmacro add-amount (expression)
  #`(+ ,'amount ,expression))

(let ((amount 10))
  (add-amount 3)) ; => 13
```

Here the macro deliberately depends on the caller providing `amount`. Use this
only when that dependency is part of the macro's contract. Forms inserted from
macro arguments also retain their caller-supplied names.

## Give local names to expressions

Use `symbol-macrolet` when a bare name should expand to an expression, such as a
table-field access.

```otium
(let ((cell {:x 1}))
  (symbol-macrolet
      ((x (get cell :x)
          (value (put! cell :x value))))
    (set! x (+ x 10))
    x)) ; => 11
```

Each read of `x` becomes `(get cell :x)`. The optional `(value write-form)`
supplies the writer: `set!` evaluates its right-hand side once, binds it to
`value`, then runs the write form. Without a writer, assigning to the symbol
macro is an error.

The replacement uses the scope where the symbol macro was defined. Inner lexical
bindings shadow it, and quoted occurrences remain data. These bindings are
local; there are no global symbol macros.

## Inspect and update expansions

Inspect a macro's output with `(macroexpand-1 '(twice 5))`. It expands one outer
macro call. `macroexpand` follows successive outer macro calls, but neither
function recursively expands the whole result.

Function bodies expand when their definitions are evaluated. After changing a
macro, re-evaluate or reload the definitions that call it. Existing callers keep
their old expansions; calling them again does not re-expand them. See [Live
programming](live-programming.md) for reloading code.

<!-- generated from macros.md.spec sha256:86031a43c2afbf89227606b9898746aba372335b609917b37a4f967d49e6e41d -->
