# ✨ Namespaces

## Choose a namespace

Start a module with `ns`, then define its values and functions:

```otium
(ns app.counts)

(def- (twice n) (* 2 n))

(def (double-count n)
  (twice n))
```

`ns` creates the namespace if needed and makes it current for the following
top-level forms. Definitions belong to that namespace. Core functions such as
`*` are available automatically.

Use `def` for public definitions and `def-` for private helpers. Both support
function and value definitions. Private names cannot be imported with `:refer`,
though qualified access can still reach them; privacy is not an access barrier.

## Put modules on the load path

Module names map to files by replacing dots with directory separators and adding
`.ot`. For example, `otium.extra` is provided by `lib/otium/extra.ot`, with
`lib` as the load-path root.

Add a root with the command-line option `-L directory`. For that example, use
`-L lib`. You can repeat `-L`; directories are searched in order.

When writing your own module, make its `ns` name match the module name used by
callers.

## Load and use a dependency

Use `require` with `:as` to give a dependency a short local name:

```otium
(require otium.extra :as extra)

(get (extra/frequencies [:a :a :b]) :a)
;; => 2
```

The slash separates the namespace alias from the definition name. `require`
loads the module if it has not already been loaded; ordinary repeated requires
do not reevaluate it.

Use `:refer` when you want selected public names available without a prefix:

```otium
(require otium.extra :refer (frequencies))

(get (frequencies [:a :a :b]) :a)
;; => 2
```

You can combine `:as` and `:refer` in the same require. Prefer an alias when it
helps readers see where a name comes from.

## Declare dependencies in `ns`

At the top of a module, put dependencies in an `ns` form with a `:require`
clause:

```otium
(ns app.counts
  (:require (otium.extra :as extra)))

(def (count-a items)
  (get (extra/frequencies items) :a 0))

(count-a [:a :a :b])
;; => 2
```

Each dependency has its own parenthesized specification. Those specifications
accept the same `:as` and `:refer` options as `require`.

## Keep references live

Calls through namespace names, aliases and referred names see later
redefinitions, even inside functions defined earlier. You do not need to import
a name again after redefining it.

Saving a function value is different: that saved value keeps its old code.

See [Live programming](live-programming.md) for reloading modules and preserving
state across reloads.

<!-- generated from namespaces.md.spec sha256:6da9d81cdb6367b58333f7cfecc7c3dd90ff61d408327eb11dad045f535733d1 -->
