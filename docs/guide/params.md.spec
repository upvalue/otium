---
title: "Dynamic params"
prompt: |
  Write a guide to dynamically scoped settings in Otium for a programmer who knows some
  Lisp and is learning Otium. They should come away knowing when a setting belongs in a param rather than
  an argument or a global, and how to set one for a stretch of code. Point out
  that params work like Scheme's `parameterize`, with `(setting)` to read one.
---

## Sources

`specs/language.md` section 9.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- `defparam`, reading with `(setting)`, `with-params`.
- When to use a param instead of an argument.
