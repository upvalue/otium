---
id: lan-qncj
status: open
deps: []
links: [lan-s095]
created: 2026-08-22T16:53:15Z
type: chore
priority: 2
assignee: Phil
tags: [gc]
---
# Guard overflow-card scan against unset card starts

recover_mark_overflow (src/ot-gc-gen.c:577) indexes starts[card] without checking for GC_START_NONE. Its sibling minor_scan_remembered has the guard. It is safe today only because major_mark_slot sets overflow bits on cards containing an object start, and record_block_start guarantees those cards have a start entry. If that invariant drifts, the scan pointer becomes old_base + UINT32_MAX * 8 and walks off the heap.

## Design

Add the same continue-on-GC_START_NONE guard minor_scan_remembered uses. One line. Optionally an assert-style abort instead, since an overflow bit on a startless card would indicate a real bug rather than a skippable card.

## Acceptance Criteria

1. recover_mark_overflow skips or aborts on a GC_START_NONE card instead of computing an out-of-range scan pointer.
2. Existing mark-stack overflow test (eight-entry stack) still passes.

