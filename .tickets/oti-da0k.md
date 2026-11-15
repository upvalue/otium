---
id: oti-da0k
status: open
deps: []
links: []
created: 2026-11-10T04:16:13Z
type: task
priority: 3
assignee: Phil
parent: oti-gwlk
tags: [threads, design]
---
# Strawman: run just-woken threads sooner

STRAWMAN FOR DISCUSSION. The run queue is FIFO with a fixed 1 ms slice and no priorities. A thread woken by I/O, a message or a timer goes to the back, so with N busy threads a UI or actor reply waits about N ms. Fds are polled at most once a millisecond while anything is runnable. Options: put woken threads at the front (Gambit's boost), or a two-level queue. Open question 3 in scratch/green-threads.md. Measure with Manse's frame time first.

