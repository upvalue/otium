---
title: "Concurrency"
prompt: |
  Write a guide to running work concurrently in Otium for a programmer who knows some
  Lisp and is learning Otium. They should come away knowing when to use a thread and when to use an actor,
  and what happens when one fails. Say plainly that threads take turns on one
  native thread, so they help with waiting, not with using more cores.
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
