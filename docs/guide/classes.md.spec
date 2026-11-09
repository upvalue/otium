---
title: "Records and classes"
prompt: |
  Write a guide to records, classes and generic functions in Otium for a
  programmer who knows some Lisp and is learning Otium. They should come away
  knowing when to reach for a table, a record or a class, and how generic
  functions let behaviour vary by type and change while a program runs.
---

## Theme

The examples build a small text adventure, one piece at a time, and keep the
same names through the page:

- A `Position` record holds a room's coordinates.
- `Item` and `Creature` are abstract types. `Sword` and `Potion` are items;
  `Troll` and `Bat` are creatures.
- The generic `describe-thing` gives each thing a line of text.
- The generic `(use item target)` dispatches on both arguments: a sword used
  on a troll does something different from a potion used on a bat.
- An `:around` method on `use` makes a cursed item misfire or logs every use,
  without changing the primary methods.
- While the game runs, a `health` slot is added to a creature class, and
  existing creatures pick it up.

No jokes in the code. Keep each example short.

## Sources

`specs/records.md`, `specs/classes.md` (describe what is implemented, not what
is deferred), `examples/hand/animals-multimethods.ot`, `tests/records/`,
`tests/classes/`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

Open with the choice the reader faces. Tables are the default for loose data.
A record is for data with a fixed set of fields, where a misspelt field should
be an error and the shape should be cheap. A class is for values whose
behaviour depends on what kind of thing they are, through generic functions.

Then, in order:

1. Records: declaring one, constructing it, reading and setting fields,
   `:immutable`. Records have no methods or inheritance.
2. Classes: slots, `:init` defaults, required slots, `make`, reading and
   setting slots.
3. Abstract types and `:is`. Classes are final: a class can't extend another
   class, only an abstract type. Explain why this works in practice: shared
   behaviour lives in methods on the abstract type, not inherited slots.
   This will surprise readers who know CLOS or Java, so say it plainly.
4. Generic functions and methods, starting with one argument, then
   dispatching on two. Say briefly what happens when two methods are equally
   specific.
5. Calling the next method with `:next`, and `:around` methods.
6. Changing a class while the program runs: existing instances keep working
   and pick up new slots, and `update-instance` converts old data. Link to
   [Live programming](guide/live-programming.md).

Leave out built-in classes beyond a sentence, `:state` slots, `:accessor`, and
performance.
