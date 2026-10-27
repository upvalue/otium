# ✨ Loops

Reach for `for` first when walking or building collections. `for` and `dotimes` are available everywhere without a `require`.

The dragon's hoard is a list of coin values:

```otium
(def hoard '(5 12 3 40 7))
hoard
; => (5 12 3 40 7)
```

## The shape of `for`

The shape is `(for [name] (clause ...) [=> final] body ...)`. Here, square brackets mark optional parts.

Clauses step together, and the loop stops when any driver runs out. Without `=>`, an unnamed loop returns `nil`.

Walk the hoard and record each coin:

```otium
(let ((counted []))
  (for ((coin :in hoard))
    (push! counted coin)))
; => nil
```

## Choose a driver

`(x :in seq)` walks a list, an array, or a string. Strings yield characters. Use `:collect` and `=>` to return the values visited:

```otium
(for ((coin :in hoard)
      (coins :collect coin))
  => coins)
; => (5 12 3 40 7)
```

`(k v :in table)` visits entries in insertion order. The dragon records its gem counts:

```otium
(for ((gem count :in {:ruby 3 :emerald 5 :opal 1})
      (gems :collect (list gem count)))
  => gems)
; => ((:ruby 3) (:emerald 5) (:opal 1))
```

`(i :range [start] end [step])` defaults to a start of `0` and a step of `1`. The end is exclusive: counting from 1 through 10 means `(i :range 1 11)`. A negative step counts down, as when the dragon counts days left to sleep:

```otium
(for ((day :range 3 0 -1)
      (days :collect day))
  => days)
; => (3 2 1)
```

`(x := init next)` holds a value and steps it each pass. It does not stop the loop itself; pair it with a bounded driver. Here the number of villages raided doubles each day:

```otium
(for ((day :range 1 4)
      (villages := 1 (* villages 2))
      (raids :collect [day villages]))
  => raids)
; => ([1 1] [2 2] [3 4])
```

## Step together or nest

Two drivers step side by side; they do not produce every pair. This loop stops after two coins because the day range ends:

```otium
(for ((coin :in hoard)
      (day :range 1 3)
      (checks :collect [day coin]))
  => checks)
; => ([1 5] [2 12])
```

Nest one `for` inside another to inspect every coin on every day. An inner loop cannot add to the outer loop's accumulator. To get one flat list, collect the inner lists and flatten `rows` in `=>`:

```otium
(for ((day :range 1 3)
      (rows :collect
        (for ((coin :in hoard)
              (checks :collect [day coin]))
          => checks)))
  => (apply append rows))
; => ([1 5] [1 12] [1 3] [1 40] [1 7]
;     [2 5] [2 12] [2 3] [2 40] [2 7])
```

Or push onto one shared array from the inner body:

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

Use `:collect` for a list, `:collect-array` for an array, and `:sum` for a total. Each accepts an optional `:when test`. Use their names in `=>`, which computes the result when the loop finishes.

The dragon sorts treasure into groups and weighs its gold in one pass:

```otium
(let ((treasures
        [{:name :crown :kind :gold :weight 8}
         {:name :ruby :kind :gem :weight 2}
         {:name :cup :kind :gold :weight 3}]))
  (for ((treasure :in treasures)
        (gems :collect (:name treasure)
              :when (eq? (:kind treasure) :gem))
        (heavy :collect-array (:name treasure)
               :when (> (:weight treasure) 5))
        (gold :sum (:weight treasure)
              :when (eq? (:kind treasure) :gold)))
    => [gems heavy gold]))
; => [(:ruby) [:crown] 11]
```

For another kind of fold, use a `:=` variable and read it in `=>`. Find the largest coin:

```otium
(for ((coin :in hoard)
      (largest := 0 (max largest coin)))
  => largest)
; => 40
```

## Control a named loop

A named loop has the shape `(for next (clauses) => final body ...)`. It goes round only when the body calls `(next)` in tail position. Lint warns if that call is not in tail position.

### Skip the rest of a pass

Call `(next)` early in the skip branch. Otherwise, do the work and then go round. The dragon skips small coins and gives coins worth at least 40 a special guard:

```otium
(let ((guarded []))
  (for next ((coin :in hoard))
    => guarded
    (if (< coin 10)
        (next)
        (begin
          (when (>= coin 40)
            (push! guarded coin))
          (next)))))
; => [40]
```

`(next)` still steps all clauses, including accumulators. Use `:when` to exclude a value from an accumulator.

### Stop without `=>`

If the body does not call `(next)`, the loop returns the body's value without running `=>`. Stop guarding when thieves appear:

```otium
(for next ((thieves :in [0 0 2 0]))
  => :quiet
  (if (> thieves 0)
      :thieves-caught
      (next)))
; => :thieves-caught
```

### Stop with `=>`

`(next :done)` finishes this pass, including its accumulator updates, and returns `final`. Count coins through the first coin worth 40:

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

On Chez, `for` costs the same as the named `let` you would write by hand and allocates nothing beyond what the accumulators build.

## `dotimes`

`(dotimes (i count) body ...)` runs with `i` from `0` up to, but not including, `count`. It returns `nil`; return a surrounding value when you need a result.

Record three days spent sleeping on the hoard:

```otium
(let ((days []))
  (dotimes (day 3)
    (push! days day))
  days)
; => [0 1 2]
```

## Named `let`

Scheme-style `(let loop ((x init) ...) ... (loop next ...))` gives you a tail-recursive loop. Use it when the work does not fit `for`'s clauses, such as counting coins in nested treasure piles:

```otium
(let loop ((piles (list hoard '(10 (20))))
           (gold 0))
  (cond
    ((null? piles) gold)
    ((list? (car piles))
     (loop (append (car piles) (cdr piles)) gold))
    (else
     (loop (cdr piles) (+ gold (car piles))))))
; => 97
```

For loops driven by mutable state, use `(while test body ...)` to repeat while `test` is true.
