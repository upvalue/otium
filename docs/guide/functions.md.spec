---
title: "Functions"
prompt: |
  Write a guide to writing and calling functions in Otium for a programmer who knows some Lisp and is learning Otium.
---

## Sources

`specs/language.md` sections 3.3–3.5, `lib/otium/core.ot`, `lib/otium/extra.ot`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- `fn` and `(def (name args) ...)`.
- `&optional` with defaults, rest parameters, and that they can't be combined.
- Closures and tail calls.
- `apply`, and that tables, arrays and keywords can be called.
- Useful helpers in `otium.extra` (`->`, `->>`, `partial`, `comp`).
