---
id: oti-092f
status: open
deps: []
links: []
created: 2026-10-30T04:18:16Z
type: bug
priority: 2
assignee: Phil
tags: [gc]
---
# Allocator zeroes the wrong bytes for closures

object_fields (heap.h:216) skips a code word for kind 3 (closures), but oti_alloc (internal.h:999) and oti_heap_alloc (heap.c:885) compute the opaque-byte start as header + fields*8 without that skip. For a closure the 8 bytes they zero are the last capture (set to 0, the failure value, instead of nil); the slow path also never initializes the code word. Harmless today because callers fill every capture and the code before reading. Fix in the allocator: compute the opaque start with the same rule as object_bytes. oti_alloc is inline and kind is usually constant, so the extra test folds away.

