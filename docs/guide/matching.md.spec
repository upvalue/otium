---
title: "Pattern matching"
prompt: |
  Write a guide to unpacking data with `match` in Otium for a programmer who knows some
  Lisp and is learning Otium. They should come away able to replace a chain of `get`s and `cond` tests with
  one `match` that both checks the shape of a value and names its parts.
---

## Sources

`specs/match.md`, `tests/match/`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- Matching lists, arrays and tables; binding names, `_`, quoted literals.
- Guards and fallthrough, if supported.
- A worked example dispatching on event tables.
