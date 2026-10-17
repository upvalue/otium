# Writing code for both runtimes

Runnable examples live under `examples/`. Select `c/otium` or `chez/run` at
launch time. Both read the same Otium source. `make test-library` runs the
common library contract, OTCL acceptance cases, reload demo, and completed
headless examples on both runtimes. The C library contract also runs with
collection at every allocation.

Use `def`, `fn`, sequential `let`, named `let`, `defonce`, `dotimes`, and the
shared macro and condition forms. Arrays (`[...]`), insertion-ordered tables
(`{...}`), keywords, and `nil` work on both. Only `nil` and `#f` are falsy.
`get` returns `nil` for a missing entry unless given a default. Table values
of `nil` delete the entry.

Tables, arrays, and keywords are callable. `(table key [default])` and
`(array index [default])` perform lookups; `(:key table [default])` looks up
the keyword. These also work through `apply` and higher-order functions.
They remain collection values: `procedure?` does not become true for them.

## Modules and reload

Use `(require module :as alias :refer (names) :reload)`, omitting options as
needed. Grouped specs such as `(require (a :as x) (b :as y))` and the older
quoted specs remain accepted. `ns` accepts `(:require ...)` clauses.

Names map dots to directories. Within each load-path directory, `.ot` is
preferred to `.scm`; an earlier directory takes priority over a later one.
A module's `ns` declaration must match its requested name. Circular loads
fail and clear their loading guard so the source can be fixed and retried.

Reload reevaluates definitions in the same namespace. `defonce` preserves
state, including `#f`. Deleted definitions remain; failure preserves effects
of forms that already ran. This is not an atomic reload. Require the module
again to import newly added public names. Keep aliases stable across reloads.

Calls through namespace bindings see redefinitions. Saved function values
keep their previous code. For an event callback that should follow reload,
save `(fn args (apply module/handler args))`. Running frames finish with their
existing code. Chez's `live` is an optional native convenience, not required
for portable callbacks.

## Library contracts

`shared/prelude/core.scm` supplies the derived core on both runtimes.
`map`, `filter`, `take`, `drop`, and sorting preserve list/array kind. `map`
accepts one sequence. `fold-left`, `fold-right`, `exists`, and `for-all`
accept multiple proper lists and stop at the shortest one. Native Scheme's
other traversal contracts are available through `chez/`.

`put` copies a table or array before updating it. `put!` mutates it. `assoc`
is association-list lookup; it no longer means a copying table update.
`defn` was removed; write `(def (name args...) ...)`.

Less-used conveniences live in an optional module:

```scheme
(require otium.extra :as extra)
(extra/-> 5 (+ 1) (* 2))
```

It provides `->`, `->>`, `partial`, `comp`, `group-by`, and `frequencies`.
Use `:refer` if unqualified names are wanted. Both runtimes load the same
module source.

Strings use Unicode code-point indexes. `substring` clamps endpoints and
accepts an omitted end. `string->number` returns `nil` on failure. `str`,
`string-split`, `string-join`, string search, and buffers share an API.
`buffer->string` preserves the buffer for further reads and appends.
Splitting without a separator and trimming recognize ASCII whitespace.

Conditions are Otium tables with keyword fields; error data and active
restarts are arrays. Missing condition fields, missing restarts, an
undescribed restart, and a declined signal return `nil`.

`read-file` reads a whole text file into a string and raises an error if it
cannot be read. Portable input is UTF-8 without invalid byte sequences.
`current-jiffy` uses a monotonic clock in nanoseconds;
`jiffies-per-second` returns 1000000000. `current-second` returns inexact Unix
seconds. These operations depend on the host platform and its filesystem.

`current-ns` returns a namespace symbol. `describe` returns
`"namespace/name: docstring"` for a binding, uses `no documentation` for a
binding without documentation, and returns `nil` for an unknown name.
Imports report the defining namespace. Runtime forms use the shared reference.

`append` accepts only proper lists, including its final argument. Sequence
helpers reject improper lists. Native error wording may differ; portable tests
check that an error is raised.

Display leaves a top-level string unquoted. Strings nested in lists, arrays,
and tables are quoted and escaped on both runtimes, including when using
`str` and buffers.

## Runtime boundaries

Chez keeps its numeric tower. `(/ 3 2)` is `3/2` there and `1.5` on C.
Use `quotient` for integer division and inexact operands when fractional
output must agree. Keep portable exact integers within C's range. Overflow,
large numbers, complex numbers, and exactness outside that range differ.
OTCL explicitly chooses integer or inexact division for its string results.

Chez characters, vectors, bytevectors, hashtables, continuations, multiple
values, and native syntax are available after `(require chez)` through
`chez/` names. Otium `values` returns table values as an array;
`chez/values` produces Scheme multiple values. Native syntax owns its own
subforms; do not depend on Otium macro expansion inside that boundary.

C embedding, collector controls, and extension internals stay C-specific.
The maintained extension example is `c/src/ot-ext-demo.c`, exercised by
`c/tests/ext-demo.scm` and its source fixture. The obsolete extension API
example has been removed.

This shared contract does not require Chez to pass C's entire dialect suite.
See `GAPS.txt` for remaining differences and work that is not yet covered.
