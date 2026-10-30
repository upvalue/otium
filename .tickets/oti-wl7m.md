---
id: oti-wl7m
status: open
deps: []
links: []
created: 2026-10-30T04:18:16Z
type: bug
priority: 2
assignee: Phil
tags: [gc]
---
# GC verifier crashes on a pointer outside the heap

heap.c:676-703 (OTIUM_GC_VERIFY). When it reaches a value that points outside the heap it should print which object and field led there, but it first reads the header at that address and the block metadata computed from it, so it segfaults with no message. Fix: check oti_heap_contains before any header read, report a pointer outside the heap with the parent object and field, and don't read block info for unvalidated objects in the report path.

