---
id: lan-myh9
status: open
deps: [lan-pzh6, lan-iuyk]
links: [lan-2488, lan-goky, lan-ueba]
created: 2026-08-16T20:28:18Z
type: feature
priority: 1
assignee: Phil
tags: [embedded, gc, heap, alloc]
---
# Move array, table and buffer backing storage onto the GC heap

Array items, Table entries and index, and Buffer bytes are GC objects whose actual bulk lives in the C heap, reached through raw pointers and released by the collector sweep (src/heap.h:42-68, src/heap.c:203-228). Four consequences, and the first is the one that matters for an embedded target:

heapMaxBytes is not a real memory bound. A program can exhaust the device while nominally inside its heap cap, because the cap only covers the semispace.

A compacting collector exists to stop long-running fragmentation, and pushing the bulk into malloc defeats that. Measured churn: 20000 push! calls request 1.05 MB through realloc doubling; 5000 put! calls request 688 KB.

The finalizable list and the whole sweep loop exist only to service these three types plus Foreign.

Proposal: allocate the backing storage as GC objects. Array items become a Values-inline storage object; Table entries likewise; the table index is a raw byte object, untraced like String. Growth allocates a new larger storage object and copies, and the old one becomes garbage.

The real work is not the layout, it is the invariant. heap.h:222-226 currently promises that table_get, table_put, array_get, array_push and array_reserve never allocate on the GC heap, and that callers may hold raw Values across them. That promise is load-bearing across the builtins and it stops holding the moment growth can collect. Every call site needs the re-audit that lan-pzh6 is about, so this should land after it.

One cost to be honest about: today a large array backing store is never touched by the collector. Move it in and every collection pays copy bandwidth for it. That argues for sequencing after lan-iuyk so the collector slides in place instead of copying into a fresh space, and possibly for a size threshold above which backing stores go to a non-moving large-object region. Worth measuring against lan-70gb before committing to the threshold.

## Acceptance Criteria

Array, Table and Buffer storage is GC-allocated. finalizable holds only Foreign objects. heapMaxBytes bounds total runtime memory. r7rs benchmark regression understood and accepted.


## Notes

**2026-08-16T21:39:40Z**

Done. Slots/Entries/Bytes objects; finalizable is Foreign-only; heapMaxBytes now a real bound (substrate test). Audit was unnecessary -- pzh6 landed first. Benchmarks not re-measured (Phil's call).
