# ✨ Data structures

Otium has three everyday collections for grouping values:

- Use a list for head-and-tail processing and building by prepending. Lists are
  chains of pairs, so accessing a distant element means walking through the
  list.
- Use an array for indexed access or a sequence that grows at the end. Reading
  and replacing an indexed element take constant time.
- Use a table for key-based lookup, such as settings or counts. Tables remember
  insertion order.

Unlike Clojure's vectors and maps, Otium arrays and tables are mutable. `put!`
changes an existing collection; `put` returns an updated copy. Lists also
support pair mutation, as in Scheme.

## Lists

Quote a literal list with `'(1 2 3)`, or use `list` to evaluate its elements.
`cons` adds a head to an existing tail without changing that tail. `car` reads
the head; `cdr` reads the tail.

```otium
(def xs (list 2 3))

(cons 1 xs)             ; => (1 2 3)
(car xs)                ; => 2
(cdr xs)                ; => (3)
(list-ref xs 1)         ; => 3
(append '(1) xs)        ; => (1 2 3)
(reverse xs)            ; => (3 2)
```

Indexes start at zero. `list-tail` returns the tail starting at an index; `cadr`
is shorthand for `car` of `cdr`. `member` searches for a value and returns the
matching tail, or `nil`.

`append` requires proper lists for every argument, including the last. Unlike
Scheme's `append`, it does not accept a non-list final tail. Otium's empty list,
`'()`, is distinct from `nil` and is truthy.

To change a pair directly, use `set-car!` or `set-cdr!`. Any other reference to
that pair sees the change.

```otium
(set-car! xs 20)
xs                     ; => (20 3)
```

For ordinary transformations, prefer the sequence functions below. The helpers
`first`, `rest`, `second`, `last`, and `nth` work with both lists and arrays.
`first` returns `nil` for an empty sequence, whereas `car` requires a pair.

## Arrays

Square brackets construct an array and evaluate its elements. Use `get` with a
zero-based index, or call the array itself.

`put!` replaces an existing element; it does not grow the array. `push!`
appends, and `pop!` removes and returns the last element.

```otium
(def scores [3 1 2])

(get scores 0)         ; => 3
(scores 1)             ; => 1

(put! scores 0 4)
(push! scores 5)
(pop! scores)          ; => 5
scores                 ; => [4 1 2]
```

Use `list-to-array` and `array-to-list` to convert between the two kinds.

## Tables

Braces construct a table from alternating keys and values. Both are evaluated;
keywords such as `:red` are convenient self-evaluating keys.

[`get`](/reference/otium.core/#get) returns `nil` for a missing key unless you
supply a default.

```otium
(def counts {:red 2 :blue 1})

(get counts :red)      ; => 2
(get counts :green 0)  ; => 0
```

[`put`](/reference/otium.core/#put) copies before updating, while
[`put!`](/reference/otium.core/#put!) changes the original. Both work this way
on arrays too.

Putting `nil` in a table deletes the key with either operation. A table cannot
hold a "present with nil" entry; use `#f` when you need a stored false value.

```otium
(def revised (put counts :red 3))
(get counts :red)      ; => 2
(get revised :red)     ; => 3

(put! counts :green 4)
(put! counts :blue nil)

(keys counts)                  ; => [:red :green]
(values counts)                ; => [2 4]
(get counts :blue :missing)     ; => :missing
```

`keys` and `values` return arrays in insertion order. Replacing an existing
value keeps its position. Deleting and reinserting a key moves it to the end.

`update` computes a replacement value from the old one, while `merge` combines
tables. Both return new tables. When keys overlap in a merge, the value comes
from the later table.

```otium
(update counts :red inc)
; => {:red 3 :green 4}

(merge counts {:red 10 :yellow 1})
; => {:red 10 :green 4 :yellow 1}
```

Use `update!` to change the original with the computed replacement. Unlike
Clojure's `assoc`, Otium's `assoc` looks up a key in an association list; use
`put` to update a copy of a table.

## Transforming lists and arrays

The shared sequence functions work on lists and arrays, not tables. `map`,
`filter`, `take`, `drop`, `reverse`, and sorting return the same kind of
sequence as the input: a list for a list, an array for an array. They do not
mutate the input.

`map` takes one sequence. `reduce` takes the initial accumulator before the
sequence and calls its function with the accumulator and the next element.

```otium
(map inc '(3 1 2))         ; => (4 2 3)
(map inc [3 1 2])          ; => [4 2 3]
(filter odd? [3 1 2])      ; => [3 1]
(reduce + 0 [3 1 2])       ; => 6

(take 2 '(3 1 2))          ; => (3 1)
(drop 1 [3 1 2])           ; => [1 2]
```

`reduce` returns the accumulator, so it has no kind-preservation rule. Use
`for-each` for effects rather than to build a result.

`sort` returns a copy sorted in ascending order. `sort-by` derives a sorting key
from each element. Sorting is stable: elements with equal keys retain their
relative order.

```otium
(sort '(3 1 2))            ; => (1 2 3)
(sort [3 1 2])             ; => [1 2 3]

(sort-by first [[2 :b] [1 :a] [2 :c]])
; => [[1 :a] [2 :b] [2 :c]]
```

Sorting keys must belong to one orderable category, such as numbers or strings;
integer and floating-point numbers can be mixed.

## Shallow copies

[`copy`](/reference/otium.core/#copy) makes a new outer array or table. Nested
mutable values stay shared. Copying updates such as `put` are shallow too.

```otium
(def original {:items [1 2]})
(def duplicate (copy original))

(put! duplicate :label :draft)
(push! (get duplicate :items) 3)

(get original :label)      ; => nil
(get original :items)      ; => [1 2 3]
(get duplicate :items)     ; => [1 2 3]
```

Copy a nested collection too if you need to change it independently.

<!-- generated from data-structures.md.spec sha256:fb30165e0e66a76b17d921ae8eed02fae003402f3f2e8882373c377efcbabb0f -->
