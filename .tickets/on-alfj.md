---
id: on-alfj
status: open
deps: []
links: []
created: 2026-10-28T21:24:03Z
type: chore
priority: 2
assignee: Phil
tags: [native, gc]
---
# Run just test-stress on the native branch

The GC stress suite hasn't been run since phases A and B of the thread stacks (74d4c8a, d4e6207) or the crossing fix (3a750a4). It takes about 20 minutes. Run it before treating the native branch as done.

