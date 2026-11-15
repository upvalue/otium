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


## Notes

**2026-11-10T04:16:14Z**

2026-11-09: if interrupts become a queue (oti-fj74) and waiting inside atomically gets one rule (oti-fv72), check the ticker's stop condition against both.
