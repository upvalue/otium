---
id: oti-ojws
status: open
deps: []
links: []
created: 2026-11-10T04:16:13Z
type: task
priority: 3
assignee: Phil
parent: oti-gwlk
tags: [perf, threads]
---
# Timer list and poll set are linear in waiting threads

ts->timers is a sorted array: add_timer and remove_timer are O(n), and every receive with :after, sleep and timed join goes through them. poll_fds rebuilds the pollfd array on each call and coalesces with a nested loop. expire scans every thread while any watchdog is set. Fine at today's counts (20k sleepers ran in 0.3 s); do it when a workload shows it. A binary heap with an index kept in the thread covers timers.


## Notes

**2026-11-10T05:13:26Z**

2026-11-09: timers are a heap and the poll set is built in one pass (f20f3c8). Left: expire and idle_timeout still scan every thread while any watchdog is set; only dev evaluations set them. Seen while measuring: join-all fails with 'VM stack limit exceeded' at 160k threads, since %threads builds its array on the value stack.
