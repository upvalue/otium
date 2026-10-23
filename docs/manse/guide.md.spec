---
title: "Manse guide"
prompt: |
  Write a guide to Manse, the development environment written in Otium and
  powered by Ghostty, for someone who wants to run it and change it.
---

Cover:

- What Manse is and what works today. Read everything under `manse/`.
- How to build and run it, including native dependencies (libghostty-vt from
  Nix) and how they are found. Check `manse/project.ot`, `manse/native/` and
  any Justfile recipes.
- How it is put together: the app, workspaces, sessions, terminals, the PTY
  layer, the Ghostty bindings, widgets, theme, fonts, settings and keyboard
  handling. Explain which file owns what.
- How to change it while it runs, using the development server and live
  reloading. The otium-dev skill in `.agents/skills/otium-dev/SKILL.md`
  describes the tooling.
- How to run its tests and benchmarks.

Link to the language guide for anything about Otium itself rather than
repeating it.
