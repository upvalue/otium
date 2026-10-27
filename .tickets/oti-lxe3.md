---
id: oti-lxe3
status: open
deps: []
links: []
created: 2026-10-27T03:28:29Z
type: task
priority: 2
assignee: Phil
tags: [prototype, performance, gc]
---
# Inline the common allocation path in the prototype

Every allocation, including each cons, makes two out-of-line calls. oti_alloc (runtime.c) checks host I/O re-entry, polls the interrupt flag, checks GC stress and snapshots the collection count. oti_heap_alloc (heap.c) does overflow and capacity checks, writes the header with a fresh identity, sets every field to nil, and calls memset on trailing bytes even when there are none. The VM's CONS then overwrites both fields. On deriv this is about 22% of the time: oti_heap_alloc 13.7%, oti_alloc 5.3%, memset about 2.7%.

Possible shape: a static inline bump-pointer path in heap.h that falls back to the current code when a collection is needed or in stress mode; memset only when there are trailing bytes; an allocation for callers that fill every field themselves (CONS, BOX, CLOSURE). The identity counter has to stay, since identity hashing uses it. The host I/O re-entry check could move to the public ot_* entry points instead.

Check whether make gc-lint covers the prototype, and run the GC stress tests.

## Acceptance Criteria

A pair allocation with room in the heap is inline and writes each field once. make test, sanitize and the GC stress tests pass. deriv and other allocation-heavy ports get faster.

