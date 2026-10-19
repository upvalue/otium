# Writing code for both runtimes

Both runtimes read the same Otium source; `./otium --runtime=c|chez` picks one
at launch. The `shared` test suite runs the common library contract, OTCL
acceptance cases, the reload demo and the finished headless examples on both.

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

`lib/otium/core.ot` supplies the derived core on both runtimes.
`map`, `filter`, `take`, `drop`, and sorting preserve list/array kind. `map`
accepts one sequence. `fold-left`, `fold-right`, `exists`, and `for-all`
accept multiple proper lists and stop at the shortest one. Native Scheme's
other traversal contracts are available through `chez/`.

`put` copies a table or array before updating it. `put!` mutates it. `assoc`
is association-list lookup. Define functions with `(def (name args...) ...)`.

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

Otium `/` keeps exact integer divisions as integers and converts fractional
results to floats on both runtimes. Division proceeds left to right; once a
result is a float it stays a float. Unary division returns a float.
`floor`, `ceiling`, `truncate`, and `round` return integers; `round` rounds
half away from zero. Float conversions outside C's tagged integer range
raise an error. `sqrt` returns a float, including NaN for negative inputs.

Keep portable exact integers within C's range. Integer overflow still wraps
on C and produces bignums on Chez. Chez's native numeric tower and arithmetic
remain available through `chez/` names, including `chez//`, `chez/round`,
`chez/sqrt`, and the `fl`/`fx` operations. Other host numeric behavior,
including complex results from functions such as `log`, is not unified.
OTCL explicitly chooses integer or inexact division for its string results.

Chez characters, vectors, bytevectors, hashtables, continuations, multiple
values, and native syntax are available after `(require chez)` through
`chez/` names. Otium `values` returns table values as an array;
`chez/values` produces Scheme multiple values. Native syntax owns its own
subforms; do not depend on Otium macro expansion inside that boundary.

C embedding, collector controls, and extension internals stay C-specific.
The extension example is `c/src/ot-ext-demo.c`, exercised by
`c/tests/ext-demo.ot`.

This shared contract doesn't require Chez to pass C's entire dialect suite.

## Known differences

- Integer overflow wraps on C and produces bignums on Chez. Ratios and complex
  numbers from native Chez operations stay available. C keeps its bounded
  numeric representations, embedding API and collector controls.
- Native exception wording, runtime debugging and implementation-specific
  printing differ. Numeric formatting is only guaranteed identical for the
  tested cases.
- C case conversion is ASCII; native Chez case conversion follows Unicode.
  Avoid non-ASCII case conversion in shared code.
- Invalid UTF-8 decoding and filesystem error diagnostics in `read-file` aren't
  unified. There's no common streaming file, port or directory API yet.
- Reload isn't atomic and keeps definitions removed from source. Alias
  retargeting and namespace reflection are outside the common contract.
- Chez's generic dispatch sees host values that don't exist on C. Don't assume
  native vectors, hashtables or characters dispatch like Otium collections.
- `examples/hand/lc-2-add-two-numbers.ot` is unfinished. The R7RS ports can
  still depend on C numeric behavior.
