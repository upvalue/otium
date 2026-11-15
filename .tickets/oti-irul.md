---
id: oti-irul
status: open
deps: []
links: []
created: 2026-11-10T04:16:12Z
type: task
priority: 2
assignee: Phil
parent: oti-gwlk
tags: [threads, native, design]
---
# Let Otium code called from C wait on threads

A thread can only park with no C frames of its own. Code that C calls through oti_call_list cannot wait: a handler-bind handler or a slot :init that joins a thread raises E0506 (both tried), as do custom port callbacks, foreign callbacks and macro expansion; module loading raises E0502. In the same places sleep and port I/O block the whole process instead of raising. For actors this means a handler or a port callback cannot call an actor. scratch/threaded-io.md ('Custom ports') sketches the fix: the builtin returns asking the dispatcher to make the call as ordinary Otium code and then call the builtin again.

## Design

Strawman order: handlers (conditions.c:275), slot initialisers (instances.c), custom port :read/:write/:flush. Foreign callbacks and macro expansion stay nested. Decide whether sleep and I/O under the remaining nesting should keep blocking the process or raise.

## Acceptance Criteria

actor/call works from a handler-bind handler, a slot initialiser and a custom port callback.

