---
id: oti-sans
status: open
deps: []
links: []
created: 2026-10-30T04:18:18Z
type: task
priority: 2
assignee: Phil
tags: [perf, threads]
---
# Preemption ticker runs forever once started

threads.c:342: the ticker thread starts on the first spawn or watchdog and wakes every 1 ms until the runtime is destroyed, even with one thread left and no watchdog. --dev (2 s watchdog) and Manse always have it running: 1000 wakeups/s while idle, and running code takes the slow poll path every ms. Fix: wait on a condition variable while nothing can be preempted (no other runnable thread, no watchdog); signal from spawn and watchdog.

