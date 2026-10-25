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
- Loops: `while`, `dotimes` and named `let`, then a section on `for` from
  `lib/otium/loop.ot`: its drivers and accumulators, named loops and
  `(name :done)`, the tail-call lint rule, and what it costs on Chez (see
  `benchmarks/loops.ot`). Mention that `otium.core` re-exports `for` and
  `dotimes`.
- Definitions, functions, control flow, `match`, macros. Include `&optional`
  positional defaults in `fn`, `def` and `def-`, their sequential scope, and
  the `rest-arguments` allocation lint rule. Chez avoids rest lists for
  optionals; the C runtime currently uses a compatible fallback.
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
