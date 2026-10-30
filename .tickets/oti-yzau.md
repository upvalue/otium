---
id: oti-yzau
status: open
deps: []
links: []
created: 2026-10-30T04:18:15Z
type: bug
priority: 2
assignee: Phil
tags: [threads]
---
# Scheduler setup publishes rt->threads before allocating

threads.c:384-432: rt->threads is set before the primordial K_THREAD and the :deadlock/:timeout keywords are allocated. If one fails, later calls skip setup and run with nil in those places, e.g. every nil wake is reported as an E0503 deadlock. Fix: do the managed allocations first (rooted), publish rt->threads last, and return oti_error so spawn raises a normal condition; an uncaught one makes ot_eval return false to the host. No abort needed. Same in finish(): fall back to nil when keyword() fails.

