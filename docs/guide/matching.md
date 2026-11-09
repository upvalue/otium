# ✨ Pattern matching

Use `match` to choose a case and bind pieces of a value:

```scheme
(match '(1 2)
  ((list a b) (+ a b))
  (_ 0))
;; => 3
```

`match` evaluates its input once and tries clauses in order. Each clause has a
pattern followed by its body. The first matching clause runs and returns its
last expression.

## Bind names and match literals

A bare name matches anything and binds that value within the clause. `_` matches
anything without binding it.

```scheme
(match '((1 2) (3 4))
  ((list (list _ _) (list _ d)) d))
;; => 4
```

Numbers, strings, keywords, booleans, and `nil` match literally. Quote symbols
and whole lists when you want their literal values: `'sym` matches the symbol
`sym`, and `'(1 2)` matches that list. Literals use `equal?`; an integer literal
does not match a float.

A bare name always binds, even if that name already exists. Use `(== name)` to
compare with an existing value:

```scheme
(let ((expected 5))
  (match 5
    ((== expected) 'same)
    (_ 'different)))
;; => same
```

Use each binding name only once in a pattern.

## Unpack lists and arrays

`(list a b)` matches a proper list with exactly two elements. Add `& rest` to
bind the remaining tail:

```scheme
(match '(1 2 3)
  ((list first & rest) (list first rest)))
;; => (1 (2 3))
```

For tagged lists, quote the tag and bind the remaining elements:

```scheme
(match '(move 1 2)
  ((list 'move dx dy) (+ dx dy))
  (_ 0))
;; => 3
```

Array patterns use square brackets. `[a b]` matches exactly two elements;
`[a b & _]` accepts additional elements and ignores them:

```scheme
(match [1 2 3]
  ([a b & _] (+ a b)))
;; => 3
```

Array rest patterns only support `_`, so you cannot bind an array's remaining
elements this way. List patterns do not match arrays, and array patterns do not
match lists.

## Dispatch on event tables

Table patterns name the keys to check. Those keys must be literals and must be
present; extra keys are ignored. Patterns can nest, so an event's position can
be unpacked as an array.

This function converts event tables into tagged lists:

```scheme
(def (event e)
  (match e
    ({:type :click :pos [x y]} (list 'click x y))
    ({:type :key :key k} :when (string? k) (list 'key k))
    ({:type t} (list 'other t))
    (_ 'not-an-event)))

(event {:type :click :pos [1 2]})
;; => (click 1 2)

(event {:type :key :key "a" :mods [:shift]})
;; => (key "a")

(event {:type :key :key 5})
;; => (other :key)

(event {:pos [1 2]})
;; => not-an-event
```

The click clause requires `:type` to be `:click` and `:pos` to contain exactly
two array elements. It binds those elements to `x` and `y`.

The key clause adds a guard with `:when`. The guard runs after the pattern
matches and can use its bindings. If it returns `nil` or `#f`, matching
continues with the next clause. Here, a numeric key falls through to
`{:type t}`.

Put specific cases before general ones. Once a clause succeeds, later clauses do
not run, even if its body returns a false value.

The final `_` handles values that no earlier clause accepts, including tables
without `:type`. Without a matching clause, `match` signals a `match-error`; it
does not silently return `nil`. See [Errors and recovery](errors.md) for
handling errors.

<!-- generated from matching.md.spec sha256:9b2380abba10307e05adab5e71c12d2d834b31ac04c7faf7673bbecdfed60be8 -->
