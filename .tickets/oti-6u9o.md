---
id: oti-6u9o
status: open
deps: []
links: [oti-8746]
created: 2026-10-30T02:22:29Z
type: task
priority: 2
assignee: Phil
tags: [gc, memory]
---
# Return free heap chunks and stack segments to the OS

heap_unmap_chunk only runs at heap teardown (c/src/heap.c:837), so after a spike empty chunks stay mapped. Unmap chunks whose blocks have all been free for a collection or two, or madvise(MADV_FREE) free blocks. The 8 KB native stack segment pool (segment_chunk, c/src/native.c:290) never returns memory either.

## Acceptance Criteria

Resident memory falls after a temporary allocation spike is collected.

