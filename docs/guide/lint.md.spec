---
title: "Lint and diagnostics"
prompt: |
  Write a guide to using lint and reading error reports in Otium for a programmer who knows some
  Lisp and is learning Otium. They should come away able to run lint, act on or deliberately silence a
  warning, and read an error report to find the problem in their source.
---

## Sources

`lib/otium/lint.ot`, `lib/otium/diagnostics.ot`, AGENT-LANG.md "Diagnostics and lint", `tests/lint/`, `tests/diagnostics/`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- Running lint, what kinds of warnings it gives, and how to suppress one
  with a reason.
- How errors are reported, with source locations.
