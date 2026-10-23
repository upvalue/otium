---
title: "Otium language guide"
prompt: |
  Write a guide to the Otium language for a programmer who knows some Lisp
  (Scheme or Clojure) and is new to Otium.
---

Cover, in roughly this order:

- What Otium is and how it relates to Scheme, Clojure and Janet. The two
  runtimes (Chez Scheme, the primary one, and C, which is experimental and can
  lag behind) and how to pick one with `./otium`.
- Running programs and the REPL; the `otium` subcommands people use day to day
  (run, repl, fmt, lint, test, explain, serve). Check `lib/catalog/` and
  `tools/generate-cli.py` for the real list.
- Syntax and data: literals, keywords, tables, vectors, strings.
- Definitions, functions, control flow, `match`, macros.
- Namespaces, `require`, `project.ot` and module paths.
- Errors and diagnostics: error codes, `otium explain`, lint rules and how to
  suppress one. Read `lib/catalog/diagnostics.json`.
- Live reloading and the development server, since live reloading is central
  to the project.
- The FFI, briefly, with a pointer to where bindings live.
- A short tour of the standard library under `lib/otium/`.
- Where to go next: `specs/` for designs, `examples/`, and the Manse and otos
  guides.

AGENT-LANG.md is the most up-to-date summary of the language; use it as a
starting point and confirm details in `lib/otium/core.ot`, `chez/` and `c/`.
