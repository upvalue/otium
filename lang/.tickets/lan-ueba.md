---
id: lan-ueba
status: open
deps: []
links: [lan-2488, lan-myh9, lan-goky]
created: 2026-08-16T15:36:18Z
type: chore
priority: 2
assignee: Phil
tags: [loc, heap, code-quality]
---
# heap.cpp: dedupe per-ObjType free switch and finalizable-type predicate

The 17-line switch freeing Array items / Table entries+index / Buffer dtor / Foreign finalize / Code bytes+consts is copy-pasted between ~Heap (heap.cpp:35-59) and the collectInto sweep (heap.cpp:190-215). Extract freeObjStorage(Obj*). Also fold the finalizable-type test at heap.cpp:101-103 into the same family (needs_finalizer(ObjType) predicate) so a new ObjType can't be added to one list and missed in the other — that skew is the real hazard, not the lines. ~20 lines.


## Notes

**2026-08-16T21:40:22Z**

Resolved by lan-myh9: both switches are gone, finalizable is Foreign-only.
