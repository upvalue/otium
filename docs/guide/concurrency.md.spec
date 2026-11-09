---
title: "Concurrency"
prompt: |
  Write a guide to running work concurrently in Otium for a programmer who knows some Lisp and is learning Otium.
---

## Sources

`lib/otium/thread.ot`, `lib/otium/actor.ot`, `tests/threads/`.

Read AGENT-LANG.md first. Check every example against the sources and the
tests; don't cite tests in the doc.

## Cover

- Threads in `otium.thread`.
- Actors in `otium.actor`: spawning, sending, replies, and how failures are
  reported.
- Which to reach for when.
