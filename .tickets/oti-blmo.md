---
id: oti-blmo
status: open
deps: []
links: []
created: 2026-11-10T04:16:07Z
type: task
priority: 2
assignee: Phil
parent: oti-gwlk
tags: [threads, actors, design]
---
# Strawman: make mutexes, condvars and atomically internal

STRAWMAN FOR DISCUSSION. thread/mutex, lock!, unlock!, with-lock, condvar, condvar-wait/signal!/broadcast! and atomically are public in lib/otium/thread.ot. Nothing in lib/ or manse/ uses the mutex or condvar; only tests/threads/checks.ot, atomic-write.ot and docs/guide/concurrency.md do. lib/otium/dev.ot uses atomically, %park and %make-waitq directly for its frame lock and pause state, and ports use wait queues from C.

## Design

Proposal: remove mutex and condvar from the public API (delete them if nothing internal needs them once dev.ot is looked at; otherwise move to an internal namespace). Keep atomically and %park as internal tools for the runtime library. Docs then say: shared state belongs to one actor. Questions: 1. Does dev.ot's frame lock stay on %park, or become an actor? 2. Is anything short of an actor needed for user code that guards a shared table, e.g. a public atomically? It bakes in one native thread, which is now a fixed decision. 3. status reports a running thread as :runnable and daemon! only affects join-all; tidy these while the API is being cut down?

## Acceptance Criteria

Phil has agreed what is public. General code has no lock API beyond what was agreed, and the guide matches.

