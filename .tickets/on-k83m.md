---
id: on-k83m
status: open
deps: []
links: []
created: 2026-10-28T21:24:03Z
type: task
priority: 3
assignee: Phil
tags: [native, threads, memory]
---
# Shrink the per-thread execution record

10k parked threads cost about 5.5KB each: one 8KB segment page plus about 1.5KB of thread record, args and params. oti_exec is 688 bytes and the error record another 216. Moving rarely used parts out of line, or allocating them lazily, would cut per-thread memory.

