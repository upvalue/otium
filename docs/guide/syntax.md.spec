---
title: "Syntax"
prompt: |
  Write a guide to how Otium source is read for a programmer who knows some Lisp
  and is learning Otium. They should come away able to read any Otium code they meet, and know where
  Otium's syntax differs from Scheme: square brackets make arrays, braces make
  tables, and `nil` is its own value.
---

## Sources

`specs/language.md` sections 1 and 2.6, `tests/reader/`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- Comments, whitespace and delimiters.
- Atoms: numbers, strings and escapes, characters, symbols, keywords, `nil`,
  `#t` and `#f`.
- Lists, `[...]` arrays and `{...}` tables, and that their elements are
  evaluated.
- Quote sugar: `'`, backquote, and the ``#` `` syntax quote (link to the macros
  guide rather than explaining it here).
