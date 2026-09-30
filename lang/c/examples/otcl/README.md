# otcl

`otcl.ot` is a small Tcl interpreter written in Otium. It follows the design of
[Picol](https://github.com/antirez/picol): a hand-written parser performs
word, variable, and command substitution; commands live in a table; procedures
evaluate with a fresh call frame; and a compact Pratt parser handles `expr`.

The built-in commands are `set`, `puts`, `expr`, `if`, `while`, `proc`,
`return`, `break`, and `continue`. Variables whose names start with an uppercase
letter are global. Values have Tcl's string semantics.

otcl is deliberately small and is not a strict Tcl subset. It does not include
Tcl lists, arrays, `uplevel`, packages, or file commands. `expr` supports
parentheses, unary signs, arithmetic, comparisons, `&&`, and `||`; like its
upstream inspiration, it does not short-circuit the logical operators.

Run the Fibonacci example from the repository root:

```sh
build/otium --no-project --path examples/otcl examples/otcl/demo.ot
```

As a library, create an interpreter and pass it source strings:

```lisp
(require '(otcl :as otcl))

(define tcl (otcl/make-interpreter))
(otcl/eval-script tcl "set greeting hello; set greeting")
```

`eval-script` returns the final string result and raises an Otium error for an
otcl error. `eval-result` exposes the tagged result directly. Otium code can
extend an interpreter with `register-command!`; handlers receive the
interpreter, current variable frame, and an array of string arguments.
