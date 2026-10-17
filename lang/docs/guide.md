# User guide

This guide introduces the C dialect through examples you can run in the [playground](playground.md). Read the [reference](reference.md) for individual bindings.

> ✨ LLM-generated draft. Examples are checked against the WASM runtime during the site tests.

## Expressions and values

A program is a sequence of expressions. A call puts the function first, then its arguments. The last expression provides the result.

```scheme
(+ 20 22)
```

Numbers, strings, keywords, booleans, and `nil` evaluate to themselves. A symbol looks up a binding. Quote prevents evaluation: `'name` is a symbol value, and `'(1 2 3)` is a list. A semicolon starts a line comment. Strings use double quotes and support escapes such as `\n`.

```scheme
(list 42 3.5 "hello" :ready #t nil 'name)
```

Only `nil` and `#f` are false in a conditional. Zero, an empty string, and the empty list `()` are true. `nil` and `()` are different values.

## Names and functions

`def` creates or replaces a binding. A function definition can use a name and parameter list, or bind a function created with `fn`.

```scheme
(def (square x)
  "Return x multiplied by itself."
  (* x x))
(def twice (fn (x) (+ x x)))
(list (square 6) (twice 6))
```

A function returns its last expression. Functions close over lexical bindings. A rest parameter collects additional arguments.

```scheme
(def (add-to n)
  (fn (x) (+ n x)))
(def add-ten (add-to 10))
(add-ten 5)
```

Use `describe` to retrieve documentation attached to a binding. Add a docstring after a definition's parameter list, or before a value in a plain `def`.

```scheme
(def answer "The value used in this example." 42)
(describe 'answer)
```

## Local bindings and control flow

`let` introduces local names. Its bindings are sequential: later initializers can use earlier names. `if` selects one branch; an omitted alternative returns `nil`.

```scheme
(let ((x 10) (y (+ x 2)))
  (if (> y 10) y 0))
```

Use `begin` to group expressions, `when` and `unless` for conditional bodies, and `cond` for multiple branches. `and` and `or` short-circuit and return values, rather than forcing a boolean result.

Named `let` creates a loop. Calls in tail position do not accumulate call frames.

```scheme
(let sum ((n 100) (total 0))
  (if (= n 0)
      total
      (sum (- n 1) (+ total n))))
```

`set!` changes a binding. `while` repeats a body while its test is truthy.

```scheme
(let ((n 0))
  (while (< n 3) (set! n (+ n 1)))
  n)
```

## Lists, arrays, and tables

Lists are built from pairs. `cons`, `car`, and `cdr` construct and inspect them. Arrays are mutable indexed sequences. Tables map keys to values and retain insertion order.

```scheme
(def items [10 20 30])
(push! items 40)
(put! items 0 5)
(list (get items 0) (length items))
```

`get` accepts a default value. Storing `nil` in a table removes that key. Collections are also callable for lookup.

```scheme
(def settings {:theme "dark" :size 12})
(put! settings :size 14)
(put! settings :theme nil)
(list (settings :size) (get settings :theme "default"))
```

Use `equal?` for structural comparison and `eq?` for identity. Mutable collections can be shared; changing one affects every reference to it. `copy` makes a shallow copy.

## Transforming sequences

`map` transforms values, `filter` selects values, and `reduce` folds them into an accumulator. `map` and `filter` preserve whether their input is a list or an array.

```scheme
(reduce + 0
  (map (fn (x) (* x x))
    (filter even? (range 10))))
```

The result is `120`. `range` excludes its end. `for-each` runs a function for side effects and returns `nil`. Other useful operations include `take`, `drop`, `sort`, `group-by`, and `get-in`.

## Live definitions and namespaces

Namespace bindings remain live across redefinition. An existing caller looks up the current value of a namespace binding. Use `defonce` for state that should survive a reload; `def-` makes a namespace binding private.

```scheme
(def (message) "before")
(def (read-message) (message))
(def (message) "after")
(read-message)
```

`ns` selects a namespace. Qualified names refer to its public bindings.

```scheme
(ns garden)
(def (grow n) (+ n 1))
(ns user)
(garden/grow 4)
```

In native programs, `require` asks the host to load a namespace. Use launcher `--path` arguments to supply search directories; see [command-line programs](cli.md). The browser supports defining namespaces in the editor, but has no loader for external source files.

## Macros

Macros receive unevaluated forms and return new code. Backquote builds a template, comma inserts a value, and comma-at splices a sequence. Macros are unhygienic: use `gensym` when introducing temporary names that must not capture caller bindings.

```scheme
(defmacro twice-value (x)
  (let ((temp (gensym "value")))
    `(let ((,temp ,x)) (+ ,temp ,temp))))
(twice-value (+ 2 3))
```

`macroexpand-1` expands one head macro call; `macroexpand` repeats head expansion. Neither recursively expands every subform.

## Conditions and recovery

`error` raises a condition. `try` catches matching conditions after unwinding the protected computation.

```scheme
(try (error "No value available")
  (catch (error? problem)
    (condition-message problem)))
```

Handlers installed with `handler-bind` run where a condition is signaled. A handler can invoke an active restart, allowing the code that encountered the problem to supply a recovery action.

```scheme
(handler-bind ((error? (fn (problem) (invoke-restart 'use-value 42))))
  (restart-case (error "Need a value")
    (use-value (value) value)))
```

Use `defer` when cleanup must run on normal return or unwinding. See `signal`, `restart-case`, and `invoke-restart` in the reference for the full calling forms.

## Dynamic parameters

Parameters provide scoped settings. Call a parameter to read it, and use `with-params` to temporarily override its value.

```scheme
(defparam greeting "hello")
(with-params ((greeting "welcome"))
  (greeting))
```

## Running locally

From the language checkout, build the C runtime with `make`, then run `./otium`. Use `./otium program.scm` for a file. `./otium-chez` selects Chez and requires Chez Scheme. See [REPL editing](repl.md) for history and editing keys.

C and Chez are distinct runtimes. This guide does not promise identical numeric ranges or underlying value representations. The browser's 32-bit integers are especially relevant when porting code from a 64-bit native C build. Native FFI examples require a native runtime and its optional dependencies.
