---
title: "Namespaces"
prompt: |
  Write a guide to organising code into namespaces in Otium for a programmer who knows some
  Lisp and is learning Otium. They should come away able to split a program across files, require one
  namespace from another, and keep helpers private. Point out that a
  namespace's name must match its file path, as in Clojure.
---

## Sources

`specs/language.md` section 7, `tests/modules/`, `tests/library/modules.ot`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- `ns` and how module names map to files and load paths (`-L`).
- `require` with `:as` and `:refer`, and the `ns` form with `:require`.
- `def-` for private definitions.
- That references are live; link to the live programming guide for reload.
