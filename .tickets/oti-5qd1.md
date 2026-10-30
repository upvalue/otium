---
id: oti-5qd1
status: open
deps: []
links: []
created: 2026-10-30T02:22:31Z
type: task
priority: 3
assignee: Phil
tags: [native]
---
# Name the error when the global or constant reservation fills

Native builds reserve 1M global cells and 2M constants at fixed offsets (OTI_CELL_RESERVE, OTI_CONSTANT_RESERVE in c/src/internal.h:889). Reaching either reports 'out of host memory'. Say which table is full instead.

## Acceptance Criteria

Filling either table gives an error naming it.

