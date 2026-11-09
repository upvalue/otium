# ✨ Data structures

## Lists

Quote a literal list; use `list` when its elements should be evaluated. `cons`
adds a head to a tail, while `car` and `cdr` retrieve them.

```otium
(list 1 (+ 1 1) 3)       ; => (1 2 3)
(cons 1 '(2 3))          ; => (1 2 3)
(car '(1 2 3))           ; => 1
(cdr '(1 2 3))           ; => (2 3)
(append '(1 2) '(3 4))   ; => (1 2 3 4)
```

The empty list is `'()`, distinct from `nil`. Use `null?` to test for it. `car`
and `cdr` require a pair; `first` and `rest` also handle empty sequences.

Useful list helpers include:

- `cadr`, `cddr` and related compositions of `car` and `cdr`.
-  `(list-ref xs i)` for an element and `(list-tail xs i)` for a tail, using
  zero-based indexes.
-  `(member x xs)` for the first matching tail, or `nil`; `memq` compares by
  identity.
-  `(assoc key alist)` for the matching pair in an association list, or `nil`;
  `assq` compares keys by identity.
-  `length`, `reverse`, and `append`. All arguments to `append` must be proper
  lists.

Use `list-to-array` and `array-to-list` to convert between kinds.

## Arrays

`[...]` evaluates its elements and constructs a mutable array. Indexes start at
zero. Indexed access is constant-time; accessing a list by index requires
walking it.

```otium
(let ((a [1 2]))
  (put! a 0 9)
  (push! a 3 4)
  (pop! a)               ; removes and returns 4
  a)                     ; => [9 2 3]

(get [10 20] 1)          ; => 20
(get [10 20] 5)          ; => nil
```

`put!` replaces an existing array element; it does not grow the array. `push!`
appends elements. `pop!` returns `nil` on an empty array. Arrays are also
callable: `(a 0)` means `(get a 0)`.

## Tables

`{...}` evaluates alternating keys and values to construct a mutable table.
Tables keep insertion order: `keys` and `values` return arrays in that order.
Updating a key keeps its position; deleting and reinserting it moves it to the
end.

```otium
(get {:a 1} :missing 99)  ; => 99

(let ((t {:a 1})
      (other (put t :b 2)))
  (list (count t) (count other) (get other :b)))
; => (1 2 2)
```

`get` returns `nil` for a missing key unless you supply a default. `put!`
changes the original table; `put` returns an updated shallow copy.

Putting `nil` deletes a key, with either `put!` or `put`. Use `#f` if you need
to store a false value. `(contains? t key)` tests whether a key exists.

## Transforming sequences

These functions work on lists and arrays. `map`, `filter`, `take`, `drop`,
`reverse`, `sort` and `sort-by` keep the input's kind: a list produces a list,
an array produces an array.

```otium
(map inc '(1 2 3))                    ; => (2 3 4)
(filter odd? [1 2 3 4 5])             ; => [1 3 5]
(reduce + 0 [1 2 3 4])                ; => 10
(take 2 [1 2 3])                      ; => [1 2]
(drop 1 '(1 2 3))                     ; => (2 3)
(sort '(3 1 2))                       ; => (1 2 3)
(sort-by first [[2 :x] [1 :y]])        ; => [[1 :y] [2 :x]]
```

`map` accepts one sequence. `reduce` calls its function with the accumulator and
next element, starting from the supplied initial value; its result need not be a
sequence. `take` and `drop` clamp their counts to the available elements.

Sorting returns a sorted copy and is stable. Values, or derived keys for
`sort-by`, must belong to one orderable class; integers and floats can be mixed.

Use `first`, `second`, `third`, `last` and `nth` to retrieve elements; missing
elements return `nil`. `count` gives the collection's size.

## Shallow copies

`copy` makes a new array or table. Replacing an entry in the copy leaves the
original unchanged, but nested mutable values stay shared. Mutating a shared
inner array or table is visible through both collections. The same rule applies
to copies made by `put`.

<!-- generated from data-structures.md.spec sha256:ae25b98f51ffbda6fc7ff536a7159a947564d74d7b8cccb36f65d90888ec1192 -->
