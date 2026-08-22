---
id: lan-emjw
status: open
deps: [lan-iuyk, lan-myh9]
links: []
created: 2026-08-16T20:28:03Z
type: feature
priority: 1
assignee: Phil
tags: [embedded, gc, heap, alloc]
---
# Fixed-heap mode: no allocator traffic from the collector after init

heap_collect_into allocates a full to-space with ot_alloc on every collection and frees from-space after (src/heap.c:140-143, 236). Peak footprint is therefore twice the heap at exactly the moment the system is under memory pressure. Measured with a counting allocator against a 1 MiB initial heap: peak C-heap live reached 3.2 MB against a 2 MB semispace.

lan-iuyk (Compressor mark-compact) removes the to-space allocation as a consequence of its design, so most of this is that ticket. What this one adds is the embedded-specific requirement that should be folded into it: a StateConfig mode where heapBytes equals heapMaxBytes at init, the heap never grows, and the collector calls the host allocator exactly zero times after state_create returns.

That means the growth path at src/heap.c:100-103 and the post-collect doubling at :247-248 need an explicit "fixed" branch that raises rather than grows -- an out-of-memory condition the program can handle, not an ot_fatal abort. On a device, a predictable failure the host can respond to beats a slightly larger heap.

Also depends on the array/table/buffer storage moving onto the GC heap before the bound means anything. Today heapMaxBytes only caps the semispace, not the C-heap side buffers hanging off Array, Table and Buffer objects, so a fixed heap is not yet a real memory bound.

## Acceptance Criteria

A State configured with a fixed heap performs no host allocation from the collector after construction. Heap exhaustion raises a catchable condition rather than aborting.


## Notes

**2026-08-16T21:40:23Z**

Unblocked on the myh9 half: heapMaxBytes is a real bound now. Still needs lan-iuyk plus the raise-instead-of-abort path at heap.c:101.

**2026-08-22T04:39:58Z**

The selectable gen collector landed in commit 19f6265. Its collection paths use preallocated mark and card structures and do not call the host allocator, but heap exhaustion still reaches the existing must-allocate abort path. Rebase this ticket on the ot_gc_heap boundary, keep the catchable OOM requirement, and test both GC selections. Host sizing and lazy old-space metadata are tracked separately in lan-7l89.
