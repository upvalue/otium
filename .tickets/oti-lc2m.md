---
id: oti-lc2m
status: open
deps: []
links: []
created: 2026-11-10T04:16:12Z
type: task
priority: 3
assignee: Phil
parent: oti-gwlk
tags: [threads]
---
# Deadlock detection misses partial deadlocks and never fires under --dev

E0503 is raised only in the main thread, and only when nothing is runnable, no timer is set and no thread waits on an fd (schedule, c/src/threads.c). Two workers deadlocked on each other while main runs just stay :waiting (tried). The dev server always has a thread in %wait-fds, so detection cannot fire under --dev. Related: a thread parked for good (on a mailbox nobody can reach, or a collected queue) is never collected, since ts->all roots it (open question 4 in scratch/green-threads.md). Cheap first step: a dev-server listing of waiting threads and what each waits on.


## Notes

**2026-11-10T05:17:44Z**

2026-11-09: first step done (c6421c5): thread/waiting-on, dev :threads op, otium-dev threads. Left for Phil: what partial-deadlock detection should cover. Join cycles are visible to C and could be found when a join parks. Mutex owners live in Otium tables, so lock cycles need either the owner in C or a check in lock!. A receive can always be ended by some other thread's send, so it can't count as stuck short of a reachability check on the thread object. Under --dev, not raising E0503 seems right, since an eval can still unblock things; the listing covers that case.
