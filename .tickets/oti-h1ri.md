---
id: oti-h1ri
status: closed
deps: []
links: []
created: 2026-10-30T04:06:35Z
type: task
priority: 2
assignee: Phil
tags: [perf, compiler]
---
# Register allocator is quadratic in function size

lib/otium/compiler/alloc.ot:263 (assign). Each register's holders list keeps every vreg ever assigned to it and each new vreg runs overlap? against all of them: O(V^2) per function, and inlining makes functions bigger. The header at :3 says linear scan. Vregs are taken in order of start, so a holder whose last range ends at or before the current start can be dropped; same for slot-holders. Smaller: uses/defs take an unused fc (except :entry); successors converts between arrays and lists several times per block.

