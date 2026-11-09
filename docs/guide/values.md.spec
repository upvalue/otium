---
title: "Values and types"
prompt: |
  Write a guide to Otium's values and how they compare in Otium for a programmer who knows some Lisp and is learning Otium.
---

## Sources

`specs/language.md` section 2 and 10.1–10.2, `tests/equality/`, `tests/library/arithmetic.ot`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- The built-in types and their predicates.
- Truthiness: only `nil` and `#f` are false; `nil` is not the empty list.
- Numbers: bounded integers, floats, `/` returning floats for fractions.
- Equality: `=`, `eq?`, `equal?`, and when to use each.
