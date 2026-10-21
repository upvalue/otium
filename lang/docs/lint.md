# Lint rules

<!-- Generated from lib/catalog/diagnostics.json by tools/generate-diagnostics.py. -->

Rules checked by `otium lint`. Suppress one for a form with a comment on the line before it or at the end of its first line, `;; lint: allow(rule-name) reason`, or for a whole file with `;; lint: allow-file(rule-name) reason` before the first form. A reason is required. Run `otium explain CODE` to print an entry.

## L0001 if-chain: if in the else branch of an if

Group `style`, default level `warn`.

Nested ifs in the else position read as a ladder. `cond` lists each test and result at the same depth.

Example:

```lisp
(if a 1 (if b 2 3))
```

Corrected:

```lisp
(cond (a 1) (b 2) (else 3))
```

## L0101 foreign-definition: Scheme or Clojure definition form

Group `foreign`, default level `warn`.

Otium defines with `def`, makes functions with `fn` and has a sequential plain `let`. `define`, `defn`, `lambda`, `let*` and `letrec` from Scheme and Clojure are not defined.

Example:

```lisp
(define (twice x) (* 2 x))
```

Corrected:

```lisp
(def (twice x) (* 2 x))
```

## L0102 bracket-bindings: square brackets used as a binding list

Group `foreign`, default level `deny`.

In Otium `[a b]` builds an array. Binding and parameter lists are parenthesized: `(let ((x 1)) ...)`, `(fn (x) ...)`, `(def (f x) ...)`.

Example:

```lisp
(let [x 1] x)
```

Corrected:

```lisp
(let ((x 1)) x)
```

## L0201 unbound-name: name is not defined

Group `correctness`, default level `deny`.

After macro expansion, this name is not a local binding, a definition in this file, an import or a prelude name. At runtime it raises E0201.

Example:

```lisp
(def (f x) (frobnicate x))
```

## L0202 unused-binding: local binding is never used

Group `suspicious`, default level `warn`.

A `let` binding, parameter or `match` variable is never referenced. Remove it, or start its name with `_` to show it is intentionally unused.

Example:

```lisp
(def (f x y) x)
```

Corrected:

```lisp
(def (f x _y) x)
```

## L0900 expansion-failed: lint could not expand a form

Group `correctness`, default level `warn`.

Name checks run on macro-expanded code. The linter loads required namespaces and evaluates the file's own `defmacro` forms, but runs nothing else. A form that fails to expand, for example because a macro depends on a value computed in the same file, is skipped by the checks that need expansion.

## L0901 unused-suppression: suppression comment suppressed nothing

Group `suspicious`, default level `warn`.

A `lint: allow` directive names a rule that reported nothing in the code it covers. Remove the directive, or the rule name, so stale suppressions do not hide future problems.

Example:

```lisp
;; lint: allow(if-chain) kept for symmetry
(cond (a 1) (else 2))
```

Corrected:

```lisp
(cond (a 1) (else 2))
```

## L0902 unexplained-suppression: suppression comment without a reason

Group `suspicious`, default level `deny`.

Every `lint: allow(rule ...)` and `lint: allow-file(rule ...)` directive must say why, after the closing parenthesis, so readers can judge whether it still applies.

Example:

```lisp
;; lint: allow(if-chain)
```

Corrected:

```lisp
;; lint: allow(if-chain) mirrors the table in specs/io.md
```

## L0903 unknown-rule: suppression names an unknown rule

Group `correctness`, default level `deny`.

A `lint:` directive, or the `lint` entry of `project.ot`, names a rule that does not exist. Run `otium lint --rules` for the list.

Example:

```lisp
;; lint: allow(if-chains) typo
```

Corrected:

```lisp
;; lint: allow(if-chain) mirrors the spec table
```
