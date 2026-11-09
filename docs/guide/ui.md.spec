---
title: "User interface"
prompt: |
  Write a guide to building a user interface with `otium.ui` in Otium for a programmer who knows some
  Lisp and is learning Otium. They should come away understanding how a UI program is put together (what
  a widget is, where state lives and how an update reaches the screen), able to
  build a small interactive window, and able to change it while it runs.
---

## Sources

`specs/ui.md` (describe what is implemented, not open questions), `lib/otium/ui.ot`, `lib/otium/ui/`, `examples/ui/`, `examples/hand/intro-ui.ot`, `tests/ui/`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- What the library is and how to run a UI program.
- The update model, widgets and state.
- Input, actions and focus.
- Layout and text.
- Changing a running UI with the development server.
- Testing a UI.
