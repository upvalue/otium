---
id: oti-cd09
status: closed
deps: []
links: [oti-l8fn]
created: 2026-10-27T03:30:53Z
type: task
priority: 2
assignee: Phil
tags: [prototype, performance, gc]
---
# Grow the prototype heap by occupancy after a collection

oti_heap_alloc (prototype/heap.c) grows the heap only when a collection fails to free room for the one allocation that triggered it. Nothing looks at how full the heap still is after collecting. With most of the semispace live, each collection copies the whole live set and frees a sliver, so a program spends most of its time collecting at every heap size on the way up. At the --c.heap-max cap it can't grow out at all.

The R7RS graphs port fits this: over half its time is in evacuate and oti_heap_collect, with dispatch at 22%. Not yet confirmed with --stats.

Usual fix: after a collection, double capacity (up to the limit) when more than about half the space is still live.

## Acceptance Criteria

After a collection that leaves the heap more than about half full, the heap grows if the limit allows. graphs spends much less time in the collector. GC stress and heap growth tests pass.


## Notes

**2026-10-29T00:28:03Z**

After a collection, the heap grows when more than OTI_HEAP_LIVE_PERCENT (50) of the space is live, sized to bring it back to that. The launcher's default --c.heap-max is now 1 GiB per semispace. graphs: 16.0s -> 6.3s, 7980 -> 588 collections, settling at 128 MiB. At the old 64 MiB limit it's 6.7s. make test passes and gc-lint shows the same 8 warnings as before; GC stress tests not run yet.
