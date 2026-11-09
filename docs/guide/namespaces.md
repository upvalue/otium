# ✨ Namespaces

Namespaces let you split a program into modules while keeping their names
separate. Each namespace owns its definitions and can give short aliases to
other namespaces. Public definitions form its interface; private definitions
hold helpers.

Programs start in the `user` namespace. Use `ns` at the top of a module to
select the namespace for its definitions. Core functions such as `get` and `+`
are available automatically.

## Namespaces and files

As in Clojure, a namespace's name must match its file path. Replace dots with
directory separators and add `.ot`: the namespace `shop.report` belongs in a
file named `shop/report.ot`, relative to a load path.

A load path is a directory where Otium searches for modules. Add your source
directory with `-L directory` when running Otium. For example, if your modules
are under `src`, pass `-L src`. You can supply multiple `-L` directories; Otium
searches them in order.

To split a program across files:

1. Give each module a namespace and a matching path under your source directory.
2. Start each module with its `ns` form.
3. Use `require` in the modules that depend on it.

## Requiring a namespace

[`require`](/reference/otium.core/#require) loads a module and makes its
definitions accessible. `:as` gives the namespace a short name:

```otium
(ns shop.report)
(require otium.extra :as extra)

(extra/frequencies '(tea tea coffee))
;; => {tea 2 coffee 1}
```

The slash in `extra/frequencies` separates the namespace alias from the
definition's name. Requiring a module again normally reuses the loaded module.

Use `:refer` to make selected public names available without a prefix:

```otium
(require otium.extra :refer (frequencies))

(frequencies '(tea tea coffee))
;; => {tea 2 coffee 1}
```

Aliases make it clear where a function comes from and help avoid name clashes.
You can use both options in the same `require`.

## Declaring dependencies with `ns`

A `:require` clause inside `ns` keeps the module name and its dependencies
together. Each dependency goes in its own parenthesized specification:

```otium
(ns shop.report
  (:require (otium.extra :as extra)))
```

This replaces the separate `ns` and `require` forms above. As the module grows,
add dependency specifications inside the same `:require` clause.

## Keeping helpers private

Use `def` for definitions other modules should use, and `def-` for internal
helpers. Both support function and value definitions.

Continuing in `shop.report`:

```otium
(def- (counts items)
  (extra/frequencies items))

(def (count-item items item)
  (get (counts items) item 0))

(count-item '(tea tea coffee) 'tea)
;; => 2
```

Another module can require `shop.report` and call its public `count-item`
function. The function still resolves `counts` in its own namespace.

Private definitions cannot be imported with `:refer`, but they remain accessible
through qualified names. Privacy marks an internal interface rather than
enforcing an access barrier. Callers should use the public definitions.

## References stay live

Qualified names and names imported with `:refer` follow redefinitions. Existing
functions that call through those names see the updated definition.

A saved function value, however, keeps its old code. See [Live
programming](live-programming.md) for reloading modules and preserving state
across reloads.

<!-- generated from namespaces.md.spec sha256:5a4e4967a2433bc956580725cdb12c35bcbba8b7c612757a1af87f0a4a1f2f90 -->
