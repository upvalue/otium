# ✨ Macros

Macros let you add forms that control evaluation or introduce bindings. Prefer a
function when you only need to compute with argument values: functions evaluate
their arguments once and can be passed to `map` or stored as callbacks. Use a
macro when some arguments must remain unevaluated, or when the caller needs new
local names.

An Otium macro receives unevaluated forms as ordinary data and returns a form
that replaces its call. Its body runs during expansion; the returned code runs
later. Define a macro before the code that uses it.

## Write a code template

`defmacro` takes a name, parameters, and a body. Use a syntax-quote template,
written ``#` ``, to construct the returned code:

- `,expression` inserts one value into the template.
- `,@expression` splices a list of forms into it.
- `name#` introduces a fresh name.

This macro evaluates an expression once, then runs its body only if the result
is positive:

```otium
(ns app.macros)

(def- (positive? value)
  (> value 0))

(defmacro when-positive (expression . body)
  #`(let ((value# ,expression))
      (if (positive? value#)
          (begin ,@body)
          nil)))
```

`expression` holds the caller's form, not its result. `body` holds a list of the
remaining forms. Comma inserts the expression into the generated `let`; comma-at
inserts the body forms into `begin`.

Every `value#` in this template denotes the same fresh symbol. Each evaluation
of the template creates a new one, preventing the generated binding from
capturing a caller's variable.

## Keep expansions safe across namespaces

Syntax quote resolves free names where the template is written. Here,
`positive?` becomes `app.macros/positive?`. That qualified reference reaches the
private helper and cannot be captured by a caller's local binding.

```otium
(ns app.client)

(let ((value 10)
      (positive? (fn (x) #f)))
  (app.macros/when-positive value
    (+ value 2)))
;; => 12
```

Core and referred bindings are qualified with their defining namespaces;
namespace aliases become full namespace names. Unresolved bare names are
qualified with the template's namespace, so they can refer to helpers defined
later. Special forms such as `let` and `if` stay bare.

The template does not qualify inserted caller forms. The `value` in the example
still refers to the caller's local variable.

Use `name#` for bindings your macro owns. A bare binding name in a syntax-quote
template gets qualified, and binding a qualified name is an error. If the caller
supplies a binding name, insert it with comma instead.

Fresh names prevent capture, but you must also control how often arguments run.
Bind an expression once if you need its result more than once:

```otium
(let ((calls 0))
  (list (app.macros/when-positive
          (begin (set! calls (+ calls 1)) -1)
          (error "not reached"))
        calls))
;; => (nil 1)
```

The test runs once and the body is skipped. An ordinary function accepting those
expressions directly could not skip its body argument, because arguments are
evaluated before a function is called.

## Capture a name deliberately

Sometimes a macro intentionally makes a name available to its body. `,'name`
inserts a bare symbol, bypassing template qualification:

```otium
(defmacro with-it (expression . body)
  #`(let ((,'it ,expression))
      ,@body))

(with-it (+ 2 3)
  (* it it))
;; => 25
```

This deliberately binds the body's `it`. Document such names, since they can
shadow names the caller already uses. Prefer caller-supplied binding names when
a fixed name is unnecessary.

## Backquote and Lisp expectations

Plain backquote constructs data without namespace qualification or automatic
fresh names:

```otium
(let ((tail '(b c)))
  `(a ,@tail))
;; => (a b c)
```

Use plain backquote for data and ``#` `` for code templates. Ordinary quoted
symbols inside a syntax-quote template also remain data rather than becoming
namespace references.

For a Common Lisp programmer, comma and comma-at play familiar roles. Otium's
``#` `` adds definition-site namespace qualification and `name#` fresh names.
Common Lisp-style backquote alone does not provide those protections.

For a Scheme programmer, this is not `syntax-rules`. A macro is ordinary Otium
code manipulating forms, with no pattern rules or ellipsis-driven repetitions.
Namespace qualification and explicit fresh names protect generated code; they do
not provide `syntax-rules`' automatic lexical hygiene.

## Local symbol macros

`symbol-macrolet` replaces references to a local name with an expression. This
is useful when a macro wants to expose a table field as if it were a variable.
Unlike `let`, it substitutes the expression at each reference rather than
storing its result.

An optional writer gives the name an assignment operation:

```otium
(let ((point {:x 2}))
  (symbol-macrolet ((x (get point :x)
                       (value (put! point :x value))))
    (set! x (+ x 3))
    (list x (get point :x))))
;; => (5 5)
```

Reading `x` reads the field. Assigning to `x` evaluates the new value once,
binds it to the writer's `value` parameter, and runs the writer form. Without a
writer, assignment is an error.

Inner lexical bindings shadow symbol macros, and quoted names remain data.
Symbol macros are local; there are no global symbol macros.

## Inspect and reload

Use [`macroexpand-1`](/reference/otium.core/#macroexpand-1) to inspect one
expansion of a quoted call:

```otium
(car (macroexpand-1
       '(app.macros/when-positive 3 42)))
;; => let
```

[`macroexpand`](/reference/otium.core/#macroexpand) continues expanding the
outermost macro call, but does not recursively expand every nested form.

Function bodies are expanded when their definitions are evaluated, not on each
call. Redefining a macro therefore leaves existing callers unchanged.
Re-evaluate or reload those callers' definitions to use the new expansion. See
[Live programming](live-programming.md).

<!-- generated from macros.md.spec sha256:b2846fc81e2a8dc785cfcdb1f70b406c2487f77013238f4d68a94c7561d5d247 -->
