---
title: "Data structures"
prompt: |
  Write a guide to lists, arrays and tables in Otium for a programmer who knows some
  Lisp and is learning Otium. They should come away knowing when to use a list, an array or a table, and
  how to build, read, change and transform each. Point out where Otium differs
  from Scheme and Clojure: arrays and tables are mutable, `put!` mutates while
  `put` copies, and putting `nil` in a table deletes the key.
---

## Sources

`specs/language.md` sections 2.3, 2.7, 10.3–10.5, `tests/library/sequences.ot`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

One page covering all three.

- Lists: `cons`, `car`, `cdr`, `list`, and the list helpers.
- Arrays: `[...]`, indexing, `push!`, `pop!`.
- Tables: `{...}`, insertion order, `get` with a default, `put!` vs `put`,
  and that putting `nil` deletes a key.
- Shared sequence functions: `map`, `filter`, `reduce`, `take`, `drop`,
  sorting, and that they keep the input's kind.
- `copy` is shallow; mutable values stay shared.
