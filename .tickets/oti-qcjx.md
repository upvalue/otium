---
id: oti-qcjx
status: closed
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


## Notes

**2026-11-16T03:25:37Z**

Fixed: deadline_after rounded to the nearest ns since 5524f5c, so the actor timer could wake just short of a due time, compute a sub-ns delay, poll without parking and livelock the test scheduler's clock. Positive timeouts now round up. All 8 seeds and the real scheduler pass.
