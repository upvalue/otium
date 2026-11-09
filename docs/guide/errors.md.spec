---
title: "Errors and recovery"
prompt: |
  Write a guide to handling errors in Otium for a programmer who knows some Lisp and is learning Otium.
---

## Sources

`specs/language.md` section 8, `tests/conditions/`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- Signalling errors.
- `try` with `catch`, and inspecting conditions with `condition-message`.
- `defer` and `unwind-protect` for cleanup.
- `handler-bind` and `restart-case` for recovering where the error happens,
  with one example that shows why you'd want that over `try`.
