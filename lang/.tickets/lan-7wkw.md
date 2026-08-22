---
id: lan-7wkw
status: open
deps: []
links: [lan-s095, lan-7l89, lan-qojh]
created: 2026-08-22T04:39:40Z
type: task
priority: 1
assignee: Phil
tags: [gc, performance]
---
# Reduce generational major and compaction pause time

The first equal-budget measurements show lower total GC time for gen on churn and mixed lifetimes, but higher maximum pauses in every direct workload. Forced fragmentation is also slower than semispace on throughput and pause time. Measure the major path in enough detail to explain and reduce those gaps.

## Design

Add diagnostic timing for mark, pointer repair, movement, metadata rebuild, and promotion preflight without changing the inclusive mutator-pause metric. Evaluate the chunk-release compaction policy, conservative remembered-set rebuild, cumulative mark construction, and nested major preflight. Keep compaction optional and order-preserving. Prefer policy and metadata improvements before adding collector concurrency.

## Acceptance Criteria

1. A repeatable profile attributes major and compaction time to concrete subphases.
2. The forced-fragmentation regression has an explained cost model and at least one measured improvement or a documented reason to retain it.
3. Mixed and churn throughput do not regress beyond benchmark noise.
4. Inclusive maximum pause is reported under at least two memory budgets.
5. The exact-pointer, multi-chunk object, overflow-stack, sanitizer, and extension-finalization tests remain green.
