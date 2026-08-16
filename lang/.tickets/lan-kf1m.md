---
id: lan-kf1m
status: open
deps: []
links: []
created: 2026-08-16T00:17:47Z
type: chore
priority: 3
assignee: Phil
---
# u32 arithmetic overflow guards at heap boundaries

u32 offsets/lengths are the right call for the low-memory design (indices into VM-owned structures, not pointer-width; 16-byte Value preserved on 64-bit). But u32 arithmetic can wrap before capacity checks see it: Heap::alloc's sizeof(Obj)+align8(payloadBytes), make_string_h's sizeof(StringData)+len+1, table_ensure's ncap doubling. Add explicit guards (reject payloads > maxBytes before doing the arithmetic). Optional: HeapOff/SlotIdx typedefs for intent. From 2026-08-15 discussion with Phil during lan-6mpt.

