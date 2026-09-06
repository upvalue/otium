---
id: lan-e1se
status: open
deps: []
links: []
created: 2026-08-22T20:43:31Z
type: task
priority: 2
assignee: Phil
tags: [gc, performance, config]
---
# Make the generational mark stack configurable per state

The gen collector preallocates a mark stack with `GC_MARK_STACK_ENTRIES` entries, currently 16,384. Overflow recovery is bounded and correct, but a wide live object graph can trade a small stack for repeated card scans and longer major pauses. Large host heaps should be able to spend more memory on this worklist without rebuilding the runtime.

## Design

Add `mark_stack_entries` to `ot_config` and keep `GC_MARK_STACK_ENTRIES` as the default. Allocate the selected capacity once in `ot_gc_heap_init`; do not allocate or resize the stack during marking. Add statistics for peak stack depth, overflowed objects or cards, and recovery rounds so capacity can be chosen from measurements rather than heap size alone. Compare 16K, 64K, 256K, and 1M entries on a deliberately wide graph plus the existing GC workloads. Keep the eight-entry overflow configuration as the bounded fallback test.

## Acceptance Criteria

1. An embedding can select mark-stack capacity per state without rebuilding Otium.
2. The default remains 16,384 entries and existing configuration continues to work.
3. Major collection performs no host allocation to grow the mark stack.
4. Statistics report peak stack use and enough overflow-recovery work to explain pause costs.
5. Eight-entry overflow recovery and both normal collector suites pass.
6. Measurements document memory cost, major pause, and recovery work at 16K, 64K, 256K, and 1M entries.
