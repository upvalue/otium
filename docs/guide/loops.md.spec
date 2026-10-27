---
title: Loops
prompt: |
  Write a guide to looping in Otium for a programmer who knows some Lisp and
  wants to write loops. The main subject is `for`, the loop people should
  reach for first. Shorter sections at the end cover `dotimes` and
  Scheme-style named `let` recursion. Theme every example around a dragon
  tending its hoard: counting coins, sorting gems, weighing treasure, guarding
  the cave.
---

## Theme

Every example is about a dragon and its hoard. Keep the names consistent
through the guide so the examples read as one story:

- The hoard is a list of coin values. Define it once near the top with
  `(def hoard '(5 12 3 40 7))` and use `hoard` in later examples rather than
  binding it again in each one.
- Gems are a table from gem name to count, e.g. `{:ruby 3 :emerald 5 :opal 1}`.
- Treasures are tables with `:name`, `:kind` and `:weight` keys.
- Days spent sleeping on the hoard, villages raided, thieves caught.

A short line of story before an example is fine; it should never hide what
the code does. Names like `coin`, `gem`, `hoard`, `gold` and `day` are good.
No jokes in the code itself.

## Sources

The facts are in `lib/otium/loop.ot`, `tests/library/loops.ot` and the `for`
entry in AGENT-LANG.md. Use them to check every example. Don't cite them in
the doc, and don't describe how the macro expands.

## `for`

Teach these in order, one short example each unless noted:

1. **The shape.** `(for [name] (clause ...) [=> final] body ...)`. Clauses
   step together and the loop stops when any driver runs out. With no `=>`
   the result is `nil`. Start with walking the hoard.

2. **Drivers.**
   - `(x :in seq)` over a list, an array or a string (characters).
   - `(k v :in table)`, in insertion order.
   - `(i :range [start] end [step])`. The end is exclusive, so 1 to 10 is
     `(i :range 1 11)`; say so plainly. Show counting down with a negative
     step.
   - `(x := init next)`, a variable stepped each pass.

3. **Clauses step together, they don't nest.** Show two drivers stepping side
   by side, then show nesting one `for` inside another for every pair. Say that
   an inner loop can't add to the outer loop's accumulator, and show the two
   ways to get one flat result: collect inner lists and `(apply append rows)`
   in `=>`, or `push!` onto one array from the inner body.

4. **Accumulators and `=>`.** `:collect`, `:collect-array` and `:sum`, each
   with an optional `:when test`. Their names are for use in `=>`. Show
   several results from one pass returned as an array. Show that any other
   fold is a `:=` variable read in `=>`.

5. **Named loops.** `(for next (clauses) => final body ...)` goes round only
   when the body calls `(next)` in tail position. Not calling it stops the
   loop and returns the body's value without running `=>`. `(next :done)`
   finishes this pass and returns `final`. Give each of these its own short
   example: skipping the rest of a pass, stopping without `=>`, and stopping
   with `(next :done)`. In the skipping example only the skip branch should
   call `(next)` early; use `when` or `unless` so the body reads as "skip
   this, otherwise do that, then go round". Say briefly that lint warns when
   `next` isn't in tail position.

6. **Cost.** One or two sentences: on Chez, `for` costs the same as the named
   `let` you would write by hand and allocates nothing beyond what the
   accumulators build. No numbers, no internals.

`for` and `dotimes` are available everywhere without a `require`. Say this
once, near the top.

## Shorter sections at the end

- **`dotimes`.** `(dotimes (i count) body ...)` runs the body with `i` from 0
  up to `count`. One example.
- **Named `let`.** `(let loop ((x init) ...) ... (loop next ...))`, the
  Scheme tail-recursive loop. Use it when the work doesn't fit `for`'s
  clauses, such as walking a nested structure. One example.
- One sentence on `while` for loops driven by mutable state.

Keep the guide short. Show the expected result in a comment after each
example, e.g. `; => 67`.
