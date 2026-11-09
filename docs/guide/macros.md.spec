---
title: "Macros"
prompt: |
  Write a guide to writing macros in Otium for a programmer who knows some Lisp and is learning Otium.
---

## Sources

`specs/language.md` section 6, `lib/otium/expander.ot`, `tests/syntax-quote/`, `tests/expander/`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- `defmacro` and that macros receive unevaluated forms.
- Templates with ``#` ``, comma and comma-at; namespace qualification;
  `name#` for introduced bindings; `,'name` to capture on purpose.
- Plain backquote is for data.
- Local symbol macros.
- That changing a macro means re-expanding its callers.
