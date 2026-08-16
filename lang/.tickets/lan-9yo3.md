---
id: lan-9yo3
status: open
deps: []
links: []
created: 2026-08-16T15:25:38Z
type: task
priority: 2
assignee: Phil
tags: [gc, vm, concurrency]
---
# GC walker region check: treat out-of-heap pointers as terminal

Groundwork for the process/concurrency design (Erlang-style processes, per-process heaps, pinned shared regions for code, literals, and interned symbols). The Cheney walk in collectInto currently assumes every Obj* it sees lives in from-space and must be evacuated. Once pinned shared regions exist, that assumption breaks: a pointer into shared memory must be left alone, not followed or copied.

Change: the scavenge loop gets a range check. Pointers outside the collecting heap's from-space are terminal: keep the pointer as-is, don't trace through it. Shared-region objects are immutable after publish, so not tracing through them is sound (they can't point back into any process heap).

This touches the hottest loop in the collector, so the check should be a cheap bounds compare against the from-space extent. Worth confirming with the r7rs benchmark set (lan-70gb) that the regression is in the noise.

Deliberately not in scope: growth/shrink policy retuning, transient to-space allocation, generations. Those wait until the process design actually lands.

## Acceptance Criteria

Walker leaves pointers outside from-space untouched and does not trace through them. r7rs -O2 benchmark numbers unchanged within noise.

