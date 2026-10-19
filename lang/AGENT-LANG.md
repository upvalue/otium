# Writing Otium

Agent-generated guide to idiomatic Otium. Otium is a small, Scheme-derived
Lisp with Clojure-inspired namespaces and Janet-inspired practical collections.
Chez Scheme is the primary implementation; the experimental C runtime can lag
behind. Prefer expressing features in Otium over adding runtime escape hatches.

## Everyday forms

- Use `.ot` files, kebab-case names, `?` for predicates, and `!` for mutation.
  Calls, lexical closures, quotation, and tail calls follow the Scheme tradition.
- Define values with `(def name value)`, functions with
  `(def (name arg ...) body ...)`, and anonymous functions with `(fn (arg ...) body ...)`.
  Use `def`, not `define` or the removed `defn`. The last body expression is the
  result. Optional docstrings follow the function parameter list or precede a
  value: `(def limit "Maximum entries." 100)`.
- **Plain `let` is sequential**, like Scheme's `let*`:
  `(let ((x 10) (y (+ x 2))) y)` returns `12`. Use parenthesized binding pairs.
  Named `let` is the usual tail-recursive loop; on Chez its initializers see the
  outer scope, so compute dependent initial values in a surrounding plain `let`.
- Rest parameters accept Scheme dotted syntax `(fn (x . rest) ...)`,
  `(fn (x & rest) ...)`, or a single symbol `(fn args ...)`. Rest values are lists.
- Use `if`, `cond`, `and`, `or`, `when`, and `unless`; `begin` groups expressions.
  `and` and `or` short-circuit and return values. Use `set!` to change a binding,
  `while` for a stateful loop, and `(dotimes (i count) ...)` for indexed repetition.
- **Only `nil` and `#f` are falsy.** `nil` is distinct from the empty list `'()`;
  empty lists, zero, and empty strings are truthy. Use `nil?` for missing values
  and `null?` for an empty list. An `if` without an alternative returns `nil`.

## Data and collections

- Use `'symbol` for symbol data, `:keyword` for self-evaluating tags and keys,
  `'(a b)` for literal lists, and `(list a b)` to evaluate the elements.
  `cons`, `car`, and `cdr` retain their Scheme roles.
- `[a b]` constructs a mutable array; `{:name value :count 0}` constructs a
  mutable, insertion-ordered table. Elements, keys, and values are evaluated.
  Square brackets are array syntax, not general substitutes for parentheses.
- `(get collection key)` returns `nil` when absent; supply a third argument for
  a default. Arrays use zero-based indexes. Tables, arrays, and keywords are
  callable: `(settings :size)`, `(items 0)`, `(:size settings)`.
- `(put! collection key value)` mutates; `(put collection key value)` updates a
  shallow copy. `push!` and `pop!` mutate arrays. **Putting `nil` in a table deletes
  the key**; use `#f` when a stored false value is needed. Shared mutable values
  stay shared, and `copy` is shallow. `assoc` means association-list lookup.
- Use `equal?` for structural equality, `eq?` for identity, and `=` for numbers.
- Prefer `map`, `filter`, and `(reduce f initial sequence)` for transformations;
  `for-each` is for effects. `map` takes one sequence. `map`, `filter`, `take`,
  `drop`, and sorting preserve list/array kind. `range` excludes its end.
  `append` and list helpers expect proper lists, including `append`'s final input.
- Use `match` to unpack structured data: names bind, `_` ignores, and quoted
  symbols match literally. For example,
  `(match event ({:kind :move :x x} x) (_ nil))`.
- `/` keeps exact integer results but returns floats for fractional results:
  `(/ 6 3)` is `2`, `(/ 3 2)` is `1.5`. Do not assume Scheme rational division.
  Chez supports bignums; portable code must account for C's bounded integers.

## Namespaces and live code

- Start a module with `(ns app.worker)`. Load dependencies with
  `(require app.tools :as tools)` and call `(tools/work ...)`. Dots map to
  directories: `app.tools` lives in `app/tools.ot` under a load path. The file's
  namespace must match the requested module name.
- `:refer (name ...)` imports selected names; aliases keep dependencies explicit.
  Grouped requires and `(ns app.worker (:require (app.tools :as tools)))` also work.
  Use `def-` for private namespace helpers.
- Namespace references are live: existing callers see a redefined function.
  Use `defonce` for state that should survive reloading, and ordinary `def` for
  code that should be replaced.
- A saved function value keeps its old code. For a retained callback that should
  follow redefinition, use `(fn args (apply tools/handler args))`.
- `(require app.tools :as tools :reload)` reevaluates the module. Reload preserves
  `defonce` state, leaves deleted definitions behind, and is not atomic: forms
  before an error have already taken effect. Macro changes require reexpanding
  callers, usually by reloading their definitions.

## Macros, recovery, and host boundaries

- Macros use `(defmacro name (args ...) body ...)`. They receive unevaluated
  datums and return code. Use backquote, comma, and comma-at for templates;
  macros are unhygienic, so introduce temporary names with `gensym`.
- Catch errors with `(try expression (catch (error? problem) ...))`.
  Conditions are tables; use `condition-message` to inspect the message.
  `handler-bind` and `restart-case` support recovery at the signaling site.
  `(defer expression cleanup ...)` runs cleanup on return or unwinding.
- Use `(defparam setting initial)` and `(with-params ((setting value)) ...)`
  for dynamically scoped settings; `(setting)` reads the current value.
- Load optional helpers explicitly: `(require otium.extra :as extra)` provides
  `extra/->`, `extra/->>`, `extra/partial`, `extra/comp`, and `extra/group-by`.
- When native Chez facilities are needed, `(require chez)` exposes `chez/` names.
  This is an explicit host boundary. Native syntax does not expand nested Otium
  macros. Otium `values` enumerates table values; `chez/values` produces Scheme
  multiple values. Prefer the shared Otium API for ordinary application code.
- Run a file with `./otium --runtime=chez path/to/program.ot`; add `-L directory`
  for module paths. Check `(describe 'name)`, the [shared prelude](shared/prelude/core.ot),
  and [portability notes](docs/portability.md) before assuming a familiar Scheme
  or Clojure API exists or has the same contract.
