---
id: oti-kj2x
status: open
deps: []
links: [oti-tr6n]
created: 2026-10-30T02:22:29Z
type: task
priority: 1
assignee: Phil
tags: [memory, design]
---
# Count host memory against a limit

heap_affordable (c/src/heap.c:150) counts only the GC heap and large objects. Bytecode, native code and its metadata, thread records and stacks, VM stacks, FFI memory and the global/constant tables are uncounted, which undercuts the low-memory goal. Idea: one host-bytes counter fed by long-lived allocations (oti_grow covers most), checked against a limit; scratch buffers can stay uncounted. Discuss what the limit should be and how it relates to heap-max.

## Acceptance Criteria

Long-lived host allocations are counted and exceeding the limit raises an error rather than growing without bound.

