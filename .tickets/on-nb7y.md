---
id: on-nb7y
status: open
deps: []
links: []
created: 2026-10-28T21:24:03Z
type: task
priority: 3
assignee: Phil
tags: [native, performance]
---
# Find out why ntakl is 14% slower than before threads

In the R7RS run after 3a750a4, ntakl is 1.14x d46b697 (2.78s vs 2.43s), unchanged by the segment crossing fix. Not investigated yet: could be segment crossings, the phase A bridge, or code placement. Code placement alone moves call-heavy benchmarks by about 10%, so compare with the same JIT function alignment in both builds (see the alignment ticket). See scratch/segmented-stacks.md.

