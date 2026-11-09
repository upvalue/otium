# ✨ Loops

Use `for` to walk the dragon's hoard, build collections, or keep running totals.
Drivers supply values for each pass; accumulators gather results. `for` and
`dotimes` are available everywhere without a `require`.

## The shape of `for`

`(for [name] (clause ...) [=> final] body ...)` uses brackets to mark optional
parts. Clauses step together, and the loop stops when any driver runs out. An
unnamed loop advances automatically after its body.

To inspect each coin:

```otium
(def hoard '(5 12 3 40 7))

(for ((coin :in hoard))
  (println coin))
; prints 5, 12, 3, 40, 7 on separate lines
; => nil
```

Without `=>`, normal completion returns `nil`. With `=>`, the expression after
it supplies the result. `:collect` gathers values into a list.

## Drivers

### Sequences

`(x :in seq)` walks a list, an array, or a string. Strings supply characters.

```otium
(for ((coin :in hoard)
      (coins :collect coin))
  => coins)
; => (5 12 3 40 7)
```

### Tables

`(k v :in table)` visits keys and values in insertion order. The dragon keeps
gem counts in a table:

```otium
(def gems {:ruby 3 :emerald 5 :opal 1})

(for ((gem count :in gems)
      (inventory :collect (list gem count)))
  => inventory)
; => ((:ruby 3) (:emerald 5) (:opal 1))
```

### Ranges

`(i :range [start] end [step])` defaults to start `0` and step `1`. The end is
exclusive: counting from 1 through 10 requires `(i :range 1 11)`.

A negative step counts down through the days left to sleep:

```otium
(for ((day :range 3 0 -1)
      (sleep :collect day))
  => sleep)
; => (3 2 1)
```

### Stepped variables

`(x := init next)` starts at `init` and computes a new value after each pass. It
does not stop the loop itself, so pair it with a finite driver or stop the loop
explicitly.

The dragon plans to double its gold each day:

```otium
(for ((day :range 4)
      (gold := 1 (* gold 2))
      (counts :collect gold))
  => counts)
; => (1 2 4 8)
```

## Together, not nested

Two drivers in one clause list advance side by side. This assigns one coin to
each day and stops after three days:

```otium
(for ((coin :in hoard)
      (day :range 1 4)
      (counts :collect (list day coin)))
  => counts)
; => ((1 5) (2 12) (3 3))
```

For every combination, put one `for` inside another. The dragon checks both gems
each day:

```otium
(for ((day :range 1 3)
      (rows :collect
        (for ((gem :in [:ruby :opal])
              (pairs :collect (list day gem)))
          => pairs)))
  => rows)
; => (((1 :ruby) (1 :opal)) ((2 :ruby) (2 :opal)))
```

An inner loop cannot add to the outer loop's accumulator. To get one flat list,
collect the inner lists and join them in `=>`:

```otium
(for ((day :range 1 3)
      (rows :collect
        (for ((gem :in [:ruby :opal])
              (pairs :collect (list day gem)))
          => pairs)))
  => (apply append rows))
; => ((1 :ruby) (1 :opal) (2 :ruby) (2 :opal))
```

You can also push into one shared array from the inner body:

```otium
(let ((pairs []))
  (for ((day :range 1 3))
    (for ((gem :in [:ruby :opal]))
      (push! pairs (list day gem))))
  pairs)
; => [(1 :ruby) (1 :opal) (2 :ruby) (2 :opal)]
```

## Accumulators and results

`:collect` builds a list, `:collect-array` builds an array, and `:sum` adds
numbers. Each accepts an optional `:when test` to include only matching values.
Use their names in `=>` to return the results.

The dragon sorts out gold treasures while weighing everything, returning several
results in an array from one pass:

```otium
(def treasures
  [{:name :crown :kind :gold :weight 12}
   {:name :cup :kind :silver :weight 3}
   {:name :ring :kind :gold :weight 2}])

(for ((treasure :in treasures)
      (names :collect (:name treasure)
             :when (eq? (:kind treasure) :gold))
      (weights :collect-array (:weight treasure))
      (gold :sum (:weight treasure)
            :when (eq? (:kind treasure) :gold)))
  => [names weights gold])
; => [(:crown :ring) [12 3 2] 14]
```

For another kind of fold, use a `:=` variable and read its final value in `=>`.
The dragon uses this to find its most valuable coin:

```otium
(for ((coin :in hoard)
      (largest := 0 (max largest coin)))
  => largest)
; => 40
```

## Named loops: skip or stop

In `(for next (clauses) => final body ...)`, the body controls whether another
pass happens. Call `(next)` in tail position: its result must be the result of
that branch, with no work afterward. Lint warns when `next` is not in tail
position.

### Skip the rest of a pass

Small coins need no extra guarding. Their branch calls `next` early; the other
branch records the coin before continuing:

```otium
(let ((guarded []))
  (for next ((coin :in hoard))
    => guarded
    (if (< coin 10)
        (next)
        (begin
          (push! guarded coin)
          (next)))))
; => [12 40]
```

Calling `next` still steps all clauses, including accumulators. Use an
accumulator's `:when` if it should also exclude skipped values.

### Stop with the body's value

If the body does not call `next`, the loop stops immediately and returns the
body's value without evaluating `=>`. Finding the largest coin sends the dragon
back to guard the cave:

```otium
(for next ((coin :in hoard)
           (gold :sum coin))
  => gold
  (if (= coin 40)
      :guard-cave
      (next)))
; => :guard-cave
```

### Stop with the final expression

`(next :done)` finishes the current pass, including its accumulator updates,
then returns `final`:

```otium
(for next ((coin :in hoard)
           (gold :sum coin))
  => gold
  (if (= coin 40)
      (next :done)
      (next)))
; => 60
```

## Cost

`for` costs about the same as the named `let` you would write by hand. You need
not avoid it in fast code.

## `dotimes`

`(dotimes (i count) body ...)` repeats the body with `i` from zero up to, but
not including, `count`. It returns `nil`. The surrounding expression returns the
array filled with village-raid days:

```otium
(let ((villages []))
  (dotimes (day 3)
    (push! villages day))
  villages)
; => [0 1 2]
```

## Named `let`

Scheme-style named `let` has the shape
`(let loop ((x init) ...) ... (loop next ...))`. A tail call to `loop` starts
another pass with new values.

Use it when the work does not fit `for`'s clauses, such as counting coins in
nested piles. Here `piles` holds the remaining work and `gold` holds the total:

```otium
(let loop ((piles (list hoard '(2 (8 1))))
           (gold 0))
  (cond
    ((null? piles) gold)
    ((pair? (car piles))
     (loop (append (car piles) (cdr piles)) gold))
    (else
     (loop (cdr piles) (+ gold (car piles))))))
; => 78
```

For loops driven by mutable state, use `(while test body ...)`, which repeats
while `test` is true.

<!-- generated from loops.md.spec sha256:46f9a81c2ca7a0af55b73416d1c2fbe9214b76bfe2fde8d172f004f1e9b921d2 -->
