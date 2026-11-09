---
title: "Live programming"
prompt: |
  Write a guide to changing a running Otium program in Otium for a programmer who knows some Lisp and is learning Otium.
---

## Sources

AGENT-LANG.md "Namespaces and live code", `specs/dev-server.md`, `lib/otium/dev.ot`, `.agents/skills/otium-dev/SKILL.md`, `examples/reload.ot`, `tests/dev/`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- That namespace references are live and redefinition reaches callers; a
  saved function value keeps its old code, and how to retain a callback that
  follows redefinition.
- `defonce` vs `def` across reloads.
- `require ... :reload` and what it does and doesn't do.
- Running a program with the development server and using `tools/otium-dev`
  to evaluate code and reload files.
