---
id: lan-s095
status: open
deps: []
links: [lan-7l89, lan-qojh, lan-7wkw, lan-6824]
created: 2026-08-22T04:39:51Z
type: task
priority: 2
assignee: Phil
tags: [gc, testing, ci]
---
# Automate the two-collector GC validation matrix

The generational collector was validated manually against semispace with runtime tests, CLI tests, sanitizers, exact-pointer checking, a tiny mark stack, and non-default geometry. Turn that matrix into a repeatable command and CI job so collector changes cannot silently skip one configuration.

## Design

Use separate build directories for each collector and configuration. Cover optimized semi and gen builds, ASan plus UBSan, gen with exact-pointer validation and an eight-entry mark stack, and one tuned host geometry. Run the common runtime and CLI suites for both collectors. Keep the bounded validation case separate from broad collection-on-every-allocation runs because exact object-start validation is intentionally expensive.

## Acceptance Criteria

1. One documented command runs the local GC matrix.
2. CI builds and tests both collector selections.
3. The matrix exercises ASan plus UBSan, exact-pointer validation, mark-stack overflow recovery, forced compaction, multi-card and multi-chunk contiguous objects, barriers, and extension finalization.
4. Non-default nursery, chunk, large-object, mark-stack, and heap limits compile and pass.
5. Failures identify the collector and configuration clearly.
