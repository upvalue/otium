---
title: "otos guide"
prompt: |
  Write a guide to otos, the Otium operating system: a RISC-V unikernel that
  runs the Otium C runtime, for someone who wants to build, boot and change it.
---

Cover:

- What otos is and what works today. Read everything under `otos/` except
  `otos/vendor/` and `otos/build/` (skim vendor only to say what is vendored).
- Prerequisites and how to build and boot it in QEMU. Check `otos/Justfile`,
  `otos/meson.build` and `otos/cross/`.
- How it boots: `otos/src/boot.S`, the linker script, the kernel entry, the
  console, traps and shutdown through SBI.
- How the Otium runtime is embedded: which parts of `c/` it builds, how the
  `lib/` tree is embedded as files (see `otos/tools/`) and how namespaces load.
- The REPL and line editing.
- Limits and what is missing.

Link to the language guide for anything about Otium itself rather than
repeating it.
