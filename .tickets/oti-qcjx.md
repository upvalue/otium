---
id: oti-qcjx
status: open
deps: []
links: []
created: 2026-11-15T23:34:02Z
type: bug
priority: 1
assignee: Phil
tags: [threads, actors]
---
# actors test spins until its 60 s timeout on Linux

tests/threads/actors.ot runs at 100% CPU until killed. Reproduced on HEAD 1c099f8 without any working-tree changes (x86_64 Linux, clang build).

