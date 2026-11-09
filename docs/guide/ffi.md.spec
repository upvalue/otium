---
title: "Foreign functions"
prompt: |
  Write a guide to calling C from Otium in Otium for a programmer who knows some Lisp and is learning Otium.
---

## Sources

`specs/ffi.md`, `lib/otium/ffi.ot`, `tests/ffi/`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- Loading a library and declaring functions with `otium.ffi`.
- Types, pointers and memory.
- Which runtimes support the FFI.
