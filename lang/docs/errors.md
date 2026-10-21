# Error codes

<!-- Generated from lib/catalog/diagnostics.json by tools/generate-diagnostics.py. -->

Errors raised by the reader, the expander and the runtime carry a stable code in their `:code` field. Codes are never reused. Run `otium explain CODE` to print an entry.

## E0001: unclosed form

Condition type `:read-error`.

The file ended inside a list, array, table or dotted pair. The location points at the opening delimiter that was never closed. Count delimiters from there, or run `otium fmt` on the file: its indentation usually shows where a closing delimiter went missing.

Example:

```lisp
(def (area w h)
  (* w h)
```

Corrected:

```lisp
(def (area w h)
  (* w h))
```

## E0002: unterminated string

Condition type `:read-error`.

A string literal has no closing double quote before the end of the file. Strings may span lines, so the missing quote can be far above the end of the file. Write `\"` for a double quote inside a string.

Example:

```lisp
(println "hello)
```

Corrected:

```lisp
(println "hello")
```

## E0003: unexpected closing delimiter

Condition type `:read-error`.

A `)`, `]` or `}` appeared with nothing open to close, or closes a different kind of bracket than the one that is open. Often an earlier form has one closing delimiter too many.

Example:

```lisp
(def x 1))
```

Corrected:

```lisp
(def x 1)
```

## E0004: invalid number

Condition type `:read-error`.

A token starts like a number but is not one. Otium reads decimal integers, decimal floats with an optional exponent and `0x` hexadecimal integers. A token such as `12abc` is an error, not a symbol. Rational literals such as `1/2` are not supported; write `(/ 1 2)`.

Example:

```lisp
(def ratio 1/2)
```

Corrected:

```lisp
(def ratio 0.5)
```

## E0005: reserved # token

Condition type `:read-error`.

Supported # syntax includes booleans, character literals such as `#\a`, and bytevectors such as `#u8(1 2)`. Other # syntax from Scheme or Clojure, such as `#(...)` vectors, `#{...}` sets and `#_` datum comments, is reserved. Use `[...]` for arrays.

Example:

```lisp
(def v #(1 2 3))
```

Corrected:

```lisp
(def v [1 2 3])
```

## E0006: invalid string escape

Condition type `:read-error`.

Strings accept `\n`, `\r`, `\t`, `\0`, `\e`, `\"`, `\\` and `\xHEX;` escapes, where HEX names a Unicode scalar value and the semicolon ends it. Any other character after a backslash is an error.

Example:

```lisp
(println "tab:\q")
```

Corrected:

```lisp
(println "tab:\t")
```

## E0007: malformed dotted list

Condition type `:read-error`.

A `.` inside a list must have at least one value before it and exactly one value after it, followed by the closing delimiter.

Example:

```lisp
'(1 . 2 3)
```

Corrected:

```lisp
'(1 2 . 3)
```

## E0008: empty keyword

Condition type `:read-error`.

A keyword needs a name after its colon. A bare `:` is not valid.

Example:

```lisp
{: 1}
```

Corrected:

```lisp
{:a 1}
```

## E0009: quote without a value

Condition type `:read-error`.

A `'`, backquote, `,` or `,@` must be followed by the form it applies to.

Example:

```lisp
(list 1 ')
```

Corrected:

```lisp
(list 1 '())
```

## E0010: expected value

Condition type `:read-error`.

The reader expected a value here and found none.

## E0011: invalid character literal

Condition type `:read-error`.

A character literal needs a character, a supported name such as space or newline, or a hexadecimal Unicode scalar. Surrogates and values above U+10FFFF are invalid.

Example:

```lisp
#\unknown
```

Corrected:

```lisp
#\space
```

## E0101: malformed special form

Condition type `:syntax-error`.

A special form has the wrong number or arrangement of parts, or its argument list is not a proper list. Check the form against its documentation with `(describe 'name)`.

Example:

```lisp
(if)
```

Corrected:

```lisp
(if ready? (start) (wait))
```

## E0102: invalid binding list

Condition type `:syntax-error`.

`let` takes a list of `(name value)` pairs: `(let ((x 1) (y 2)) body ...)`. Square brackets build arrays in Otium, so Clojure's `(let [x 1] ...)` is not a binding list. Plain `let` is sequential, so a later binding may use an earlier one.

Example:

```lisp
(let [x 1 y 2] (+ x y))
```

Corrected:

```lisp
(let ((x 1) (y 2)) (+ x y))
```

## E0103: invalid parameter list

Condition type `:syntax-error`.

Parameters are symbols, each named once. A rest parameter is written `(a . rest)`, `(a & rest)` or as a single symbol. Square brackets build arrays in Otium, so Clojure's `(fn [x] ...)` is not a parameter list.

Example:

```lisp
(fn [x y] (+ x y))
```

Corrected:

```lisp
(fn (x y) (+ x y))
```

## E0104: cannot rebind a core form name

Condition type `:syntax-error`.

Names of core forms such as `if`, `let`, `fn`, `def` and `quote` cannot be used as variable or parameter names. Choose another name.

Example:

```lisp
(fn (if) if)
```

Corrected:

```lisp
(fn (condition) condition)
```

## E0105: definition not allowed here

Condition type `:syntax-error`.

`def` is allowed at top level and at the start of a body, such as a function or `let` body. `def-`, `defmacro`, `ns` and `require` are allowed only at top level.

Example:

```lisp
(+ 1 (def x 2))
```

Corrected:

```lisp
(def x 2)
(+ 1 x)
```

## E0106: unquote outside quasiquote

Condition type `:syntax-error`.

`,` and `,@` are only meaningful inside a backquoted template.

Example:

```lisp
(list ,x)
```

Corrected:

```lisp
`(list ,x)
```

## E0107: invalid cond clause

Condition type `:syntax-error`.

Each `cond` clause is a list whose first element is the test: `(test body ...)`. An `else` clause needs a body.

Example:

```lisp
(cond (x) else)
```

Corrected:

```lisp
(cond (x 1) (else 2))
```

## E0108: invalid try form

Condition type `:syntax-error`.

`try` takes body expressions followed by `(catch (predicate name) body ...)` or `(catch name body ...)` clauses. Catch clauses must come after every body expression.

Example:

```lisp
(try (catch (error? e) nil) (work))
```

Corrected:

```lisp
(try (work) (catch (error? e) nil))
```

## E0109: invalid assignment target

Condition type `:syntax-error`.

`set!` assigns a variable, `(set! name value)`, or a callable place, `(set! (object key) value)`. A symbol macro can only be assigned when it has a writer.

Example:

```lisp
(set! 3 4)
```

Corrected:

```lisp
(set! counter 4)
```

## E0110: invalid ns clause

Condition type `:syntax-error`.

`ns` accepts `(:require spec ...)` clauses after the namespace name. Requires can also be written as separate `(require ...)` forms.

Example:

```lisp
(ns app.main (:use app.tools))
```

Corrected:

```lisp
(ns app.main (:require (app.tools :as tools)))
```

## E0111: macro expansion limit exceeded

Condition type `:syntax-error`.

Expanding one top-level form ran more than 10000 macro calls. A macro probably expands into a call to itself without making progress. The notes list the most recent macros expanded.

Example:

```lisp
(defmacro forever (x) `(forever ,x))
(forever 1)
```

## E0112: macro transformer failed

Condition type `:syntax-error`.

A macro's own code raised an error while it was building its expansion. The note gives the underlying error. Macros receive unevaluated forms, so the usual cause is a macro that treats a form as a value, or a call that does not match the shape the macro expects.

## E0201: unbound name

Condition type `:name-error`.

No definition, parameter, local binding or import with this name is visible here. Check the spelling, `require` the namespace that defines it, or define it. Many Scheme and Clojure names differ in Otium: use `def` rather than `define` or `defn`, and `fn` rather than `lambda`. Check `(describe 'name)` and the prelude before assuming a familiar function exists.

Example:

```lisp
(define (twice x) (* 2 x))
```

Corrected:

```lisp
(def (twice x) (* 2 x))
```

## E0202: namespace not found

Condition type `:namespace-error`.

`require` looks for `a/b.ot` for namespace `a.b` under each load path. Add the directory with `-L`, `OTIUM_PATH` or a `(paths ...)` entry in `project.ot`, or fix the namespace name.

Example:

```lisp
(require app.missing :as missing)
```

## E0203: name not defined by namespace

Condition type `:namespace-error`.

A `:refer` list or a qualified name names something the namespace does not define.

Example:

```lisp
(require otium.extra :refer (thread-first))
```

Corrected:

```lisp
(require otium.extra :refer (->))
```

## E0204: private name

Condition type `:namespace-error`.

Names defined with `def-` are private to their namespace and cannot be referred to or used qualified from another namespace. Use a public definition or move the code.

## E0205: circular namespace load

Condition type `:namespace-error`.

Namespace A requires B while B, directly or indirectly, requires A before A has finished loading. Move the shared definitions into a third namespace.

## E0206: namespace does not match file

Condition type `:namespace-error`.

The `ns` form of a file loaded by `require` must name the namespace it was found under: `app/tools.ot` must start with `(ns app.tools)`.

## E0207: definition conflicts with import

Condition type `:namespace-error`.

A definition and an import in the same namespace may not share a name. Rename the definition, or drop the name from `:refer` and use an alias instead.

Example:

```lisp
(require otium.extra :refer (comp))
(def (comp f g) ...)
```

Corrected:

```lisp
(require otium.extra :as extra)
(def (comp f g) ...)
```

## E0301: wrong number of arguments

Condition type `:arity-error`.

A function was called with more or fewer arguments than its parameter list accepts. There are no optional or keyword parameters; a rest parameter accepts any number of extra arguments. Check the signature with `(describe 'name)`.

Example:

```lisp
(def (f x) (+ x 1))
(f 1 2)
```

Corrected:

```lisp
(f 1)
```

## E0302: value is not callable

Condition type `:type-error`.

The head of a call evaluated to something that cannot be applied. Functions, tables, arrays, keywords and class instances are callable; numbers, strings, `nil` and lists are not. A common cause is extra parentheses around a value, such as `((get t :f))` when `(get t :f)` returns a number.

Example:

```lisp
(def n 3)
(n)
```

Corrected:

```lisp
n
```

## E0401: wrong type of argument

Condition type `:type-error`.

A built-in operation received a value of a type it does not accept. Remember that only `nil` and `#f` are false, that `nil` is not the empty list, and that `get` returns `nil` for a missing key.

Example:

```lisp
(car nil)
```

Corrected:

```lisp
(if (pair? xs) (car xs) nil)
```
