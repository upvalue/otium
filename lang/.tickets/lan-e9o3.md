---
id: lan-e9o3
status: open
deps: []
links: [lan-s095]
created: 2026-08-22T16:53:01Z
type: bug
priority: 1
assignee: Phil
tags: [gc, alloc]
---
# Small allocation fails while old space has room

The gen small-object path (src/ot-gc-gen.c:1107) runs one minor collection and returns NULL if the request still does not fit the nursery. A nursery full of first-cycle survivors promotes nothing -- everything is above the watermark -- so the destination fills with survivors and the allocation fails even when old space has plenty of free room. Most constructors turn that NULL into an abort through must_alloc.

The gc_stress path already runs a collection per allocation and hides this; it shows up on the normal path with a live working set near the nursery size.

## Design

After the first minor fails to make room, run a second minor: the survivors are now below the watermark, so they promote and the nursery drains. Falling back to allocate_old_with_collection for the request itself would also work but changes where small objects live. Prefer the second minor. Needs a regression test that fills the nursery with live first-cycle objects and then allocates.

## Acceptance Criteria

1. A workload whose live set fits heap_max but exceeds one nursery of first-cycle survivors allocates without returning NULL.
2. The retry does not loop: at most one extra minor per allocation.
3. Regression test covers the nursery-full case under both normal and gc_stress builds.

