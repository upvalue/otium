---
title: "Bindings"
prompt: |
  Write a guide to naming values in Otium for a programmer who knows some Lisp
  and is learning Otium. They should come away knowing how to name things at the top level and
  locally, how to change a binding, and which definitions survive a reload.
  Point out that plain `let` is sequential like Scheme's `let*`, and that
  `def` replaces `define`.
---

## Sources

`specs/language.md` sections 3.1 and 4, `lib/otium/core.ot`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- `def` for values and functions, docstrings.
- `let` is sequential (like Scheme `let*`); named `let` briefly, linking to
  the loops guide.
- `set!`.
- `defonce`, with a pointer to the live programming guide.
