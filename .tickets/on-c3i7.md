---
id: on-c3i7
status: open
deps: []
links: []
created: 2026-10-28T21:24:03Z
type: task
priority: 2
assignee: Phil
tags: [native, performance]
---
# Inline the type predicates pair?, list? and array? in native code

deriv makes about 670M builtin calls through dispatch, mostly list?, array? and pair?. Since phase A (74d4c8a) every such call also pays the C-stack bridge, 2-3 cycles each: deriv +7% and browse +5% in cycles:u against f2ff283. Compiling these predicates inline would skip both dispatch and the bridge. See scratch/segmented-stacks.md.

## Acceptance Criteria

deriv and browse measured with perf stat cycles:u before and after, against the pre-thread build d46b697.

