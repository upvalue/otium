# ✨ Pattern matching

Use [`match`](/reference/otium.core/#match) to check a value's shape and use its
parts. It combines the tests you might put in a `cond` with the bindings you
would otherwise build from `car`, `cdr`, or `get`.

A `match` contains a value expression followed by clauses. Each clause has a
pattern and a body. Patterns describe the expected structure and name the pieces
you want to use.

```otium
(match '(move 3 4)
  ((list 'move dx dy) (list dx dy))
  (_ :unknown))
;; => (3 4)
```

The pattern `(list 'move dx dy)` requires a three-element list beginning with
the symbol `move`. It binds the remaining elements to `dx` and `dy`.

`match` evaluates its input once and tries clauses from top to bottom. The first
matching clause runs, and its last body expression becomes the result.

## Names, literals, and `_`

Within a pattern:

- A bare name matches anything and binds that value for the clause's body.
- `_` matches anything without binding it.
- Numbers, strings, keywords, booleans, and `nil` match literal values.
- Quotation matches data literally: `'move` matches the symbol `move`, and
  `'(1 2)` matches that whole list.

Bare names always bind, even if a variable with that name already exists. Use
`(== expected)` to compare against an existing variable's value instead. Each
binding name must be unique within a pattern.

A final `_` clause provides a fallback. If no clause matches, `match` signals a
`match-error`; it does not silently return `nil`.

## Lists and arrays

A `(list ...)` pattern requires exactly the listed number of elements. Add
`& name` to bind the remaining list:

```otium
(match '(log "started" "ready")
  ((list 'log & messages) messages)
  (_ '()))
;; => ("started" "ready")
```

The tail can be empty. Use `& _` if you only care about the prefix.

Array patterns use square brackets. They also require an exact length unless
they end in `& _`:

```otium
(match [10 20 :unused]
  ([x y] (list :point x y))
  ([x _ & _] x)
  (_ nil))
;; => 10
```

The first clause fails because the array has three elements. The second requires
at least two, binds the first to `x`, and ignores everything else. Arrays
support ignoring the rest with `& _`, but not binding it to a name.

List patterns match lists, and array patterns match arrays; they do not
substitute for each other.

## Tables and nested patterns

A table pattern specifies required keys and patterns for their values, ignoring
extra keys. Keys must be literals, such as keywords or strings.

Patterns can nest, so a table's value can itself be unpacked as an array:

```otium
(match {:type :click :pos [10 20] :button :left}
  ({:type :click :pos [x y]} (list x y))
  (_ nil))
;; => (10 20)
```

This pattern requires a table with `:type` equal to `:click` and a `:pos`
containing a two-element array. The `:button` key does not affect the match. The
clause fails if a required key is missing, a collection has the wrong kind, or
the array has the wrong length.

Binding a name does not check its type, so `x` and `y` can be any values.

## Dispatching on event tables

A guard adds a test after the pattern has matched. Write `:when` followed by an
expression before the body. The guard can use the pattern's bindings. If it
returns `#f` or `nil`, matching continues with the next clause.

This dispatcher returns an action description rather than performing an action:

```otium
(def (dispatch-event event)
  (match event
    ({:type :click :pos [x y]}
     (list 'click x y))
    ({:type :key :key k} :when (string? k)
     (list 'insert k))
    ({:type t}
     (list 'unhandled t))
    (_ :not-an-event)))

(dispatch-event {:type :click :pos [10 20] :button :left})
;; => (click 10 20)

(dispatch-event {:type :key :key "a"})
;; => (insert "a")

(dispatch-event {:type :key :key 5})
;; => (unhandled :key)

(dispatch-event {:type :click :pos [10]})
;; => (unhandled :click)

(dispatch-event {:pos [10 20]})
;; => :not-an-event
```

The key clause checks the table's shape before testing whether `k` is a string.
A failed guard falls through just like a failed pattern. The malformed click
also falls through because its position array has the wrong length.

Keep specific clauses before broad ones: `{:type t}` accepts any table
containing `:type`, so placing it first would hide both event-specific clauses.
The final `_` handles values that lack even that basic shape.

<!-- generated from matching.md.spec sha256:dce9204f767be85c838ed6d8120dfe0779e8f0ac63980ee6713662b38107df26 -->
