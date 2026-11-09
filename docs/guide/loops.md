# ✨ Loops

Reach for `for` first when walking or building collections. `for` and `dotimes`
are available everywhere without a `require`.

## The shape of `for`

The shape is `(for [name] (clause ...) [=> final] body ...)`, where square
brackets mark optional parts. Clauses step together, and the loop stops when any
driver runs out. Without `=>`, an unnamed loop returns `nil`.

The dragon walks its hoard, putting each coin on the counting tray:

```otium
(def hoard '(5 12 3 40 7))

(let ((tray []))
  (for ((coin :in hoard))
    (push! tray coin)))
; => nil
```

## Choose a driver

`(x :in seq)` walks a list, an array, or a string. Strings yield characters. Use
`:collect` and `=>` to return the coins in order:

```otium
(for ((coin :in hoard)
      (coins :collect coin))
  => coins)
; => (5 12 3 40 7)
```

`(k v :in table)` walks entries in insertion order. The dragon inventories its
gems:

```otium
(let ((gems {:ruby 3 :emerald 5 :opal 1}))
  (for ((gem count :in gems)
        (inventory :collect (list gem count)))
    => inventory))
; => ((:ruby 3) (:emerald 5) (:opal 1))
```

`(i :range [start] end [step])` defaults to start `0` and step `1`. The end is
exclusive: counting from 1 through 10 requires `(i :range 1 11)`. A negative
step counts down, here through the days left to sleep on the hoard:

```otium
(for ((day :range 3 0 -1)
      (days :collect day))
  => days)
; => (3 2 1)
```

`(x := init next)` starts at `init` and computes a new value each pass. The
dragon doubles the number of villages raided each day:

```otium
(for ((day :range 1 5)
      (villages := 1 (* villages 2))
      (raids :collect (list day villages)))
  => raids)
; => ((1 1) (2 2) (3 4) (4 8))
```

## Step together or nest

Multiple drivers step side by side; they do not nest. This assigns one coin to
each of two days, then stops because the day range ends:

```otium
(for ((day :range 1 3)
      (coin :in hoard)
      (checks :collect (list day coin)))
  => checks)
; => ((1 5) (2 12))
```

For every day-and-coin pair, put one `for` inside another:

```otium
(for ((day :range 1 3)
      (rows :collect
        (for ((coin :in hoard)
              (checks :collect (list day coin)))
          => checks)))
  => rows)
; => (((1 5) (1 12) (1 3) (1 40) (1 7))
;     ((2 5) (2 12) (2 3) (2 40) (2 7)))
```

An inner loop cannot add to the outer loop's accumulator. To get one flat list,
collect inner lists and join them with `(apply append rows)` in `=>`:

```otium
(for ((day :range 1 3)
      (rows :collect
        (for ((coin :in hoard)
              (checks :collect (list day coin)))
          => checks)))
  => (apply append rows))
; => ((1 5) (1 12) (1 3) (1 40) (1 7)
;     (2 5) (2 12) (2 3) (2 40) (2 7))
```

For a flat array, let both loops share one array and call `push!` from the inner
body:

```otium
(let ((checks []))
  (for ((day :range 1 3))
    (for ((coin :in hoard))
      (push! checks [day coin])))
  checks)
; => [[1 5] [1 12] [1 3] [1 40] [1 7]
;     [2 5] [2 12] [2 3] [2 40] [2 7]]
```

## Accumulate results

Use `:collect` for a list, `:collect-array` for an array, and `:sum` for a
total. Each accepts an optional `:when test` that filters its contributions. Use
the accumulator names in `=>`.

The dragon sorts out gems and weighs its gold in one pass, returning several
results as an array:

```otium
(let ((treasures [{:name :ruby :kind :gem :weight 2}
                  {:name :crown :kind :gold :weight 8}
                  {:name :opal :kind :gem :weight 1}]))
  (for ((treasure :in treasures)
        (gems :collect (:name treasure)
              :when (eq? (:kind treasure) :gem))
        (heavy :collect-array (:name treasure)
               :when (> (:weight treasure) 1))
        (gold :sum (:weight treasure)
              :when (eq? (:kind treasure) :gold)))
    => [gems heavy gold]))
; => [(:ruby :opal) [:ruby :crown] 8]
```

For another kind of fold, use a `:=` variable and read its final value in `=>`.
This finds the largest coin:

```otium
(for ((coin :in hoard)
      (largest := 0 (max largest coin)))
  => largest)
; => 40
```

## Control a named loop

In `(for next (clauses) => final body ...)`, the body goes round only by calling
`(next)` in tail position. Lint warns when that call is not in tail position.

To skip the rest of a pass, call `(next)` in the skip branch. Here the dragon
skips small coins, sets aside the remaining even coins, then goes round:

```otium
(let ((gold []))
  (for next ((coin :in hoard))
    => gold
    (if (< coin 10)
        (next)
        (begin
          (when (even? coin)
            (push! gold coin))
          (next)))))
; => [12 40]
```

Calling `(next)` still steps the clauses, including accumulators. Use an
accumulator's `:when` to exclude a contribution.

Not calling `next` stops immediately and returns the body's value without
running `=>`. The dragon stops guarding the cave when it catches a thief:

```otium
(for next ((thieves :in [0 0 2 0]))
  => :quiet
  (if (> thieves 0)
      :thieves-caught
      (next)))
; => :thieves-caught
```

Call `(next :done)` to finish this pass, including its accumulator
contributions, and return `final`. The dragon counts through the first coin
worth 40:

```otium
(for next ((coin :in hoard)
           (gold :sum coin))
  => gold
  (if (= coin 40)
      (next :done)
      (next)))
; => 60
```

### Cost

On Chez, `for` costs the same as the named `let` you would write by hand and
allocates nothing beyond what the accumulators build.

## Repeat with `dotimes`

`(dotimes (i count) body ...)` runs with `i` from zero up to, but excluding,
`count`. It returns `nil`; return a surrounding value when you need a result.

The dragon records three nights guarding the cave:

```otium
(let ((guarded-nights []))
  (dotimes (day 3)
    (push! guarded-nights day))
  guarded-nights)
; => [0 1 2]
```

## Recur with named `let`

Use Scheme-style named `let`, `(let loop ((x init) ...) ... (loop next ...))`,
when the work does not fit `for`'s clauses. Keep the recursive call in tail
position.

The dragon counts coins stored in nested chests, including its hoard:

```otium
(let loop ((chests (list hoard '(2 (8))))
           (gold 0))
  (cond
    ((null? chests) gold)
    ((list? (car chests))
     (loop (append (car chests) (cdr chests)) gold))
    (else
     (loop (cdr chests) (+ gold (car chests))))))
; => 77
```

For a loop driven by mutable state, use `(while test body ...)` to repeat while
the test is truthy.

<!-- generated from loops.md.spec sha256:f4f80e595ed807feebb39365f553f75ec6989c244d61cb78d3436b6fe69f71f2 -->
