---
id: oti-gwlk
status: open
deps: []
links: []
created: 2026-11-10T04:16:07Z
type: epic
priority: 2
assignee: Phil
tags: [threads, actors, design]
---
# Finish the actor model and make it the one concurrency model

The thread system is part-way into actors: mailboxes, receive, actor/call and monitors exist, but mutexes, condvars and atomically are public beside them, messages are shared by reference, and failure handling is opt-in. Direction agreed with Phil 2026-11-09: actors are the model for general code. Locks and wait queues stay only as far as the runtime and library need them, and are not exposed. 1:n green threads stay. Foreign calls blocking every thread is accepted for now. Children cover the design questions and the bugs found in the 2026-11-09 review.

