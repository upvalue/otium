---
id: lan-s8b9
status: closed
deps: []
links: [lan-6824]
created: 2026-08-22T15:51:25Z
type: task
priority: 1
assignee: Phil
tags: [gc, gsgc, performance]
---
# Compact the GSGC private object header

Reduce GSGC per-object overhead without changing Otium’s shared object-header format or the semi and gen collectors.

## Design

Keep one private machine word with states for young ages, old, and old with young referents. Read object size from Otium’s intact header and store forwarding pointers in the evacuated source object header. The default policy uses states 0 through 6, which can move into three shared header bits later.

## Acceptance Criteria

GSGC’s private header is one machine word; common and sanitizer tests pass under GC=gsgc; semi and gen behavior is unchanged; documentation and benchmark evidence record the footprint change.

## Notes

**2026-08-22T16:08:03Z**

Compacted the private header from 32 bytes to one 8-byte word on the current 64-bit target. GSGC now reads size from Otium’s header, forwards through the evacuated Otium header, and represents young ages, old, and remembered-old as states 0 through 6. No shared header or other collector source changed. In 20-run medians at the equal 128 MiB geometry, old-to-compact workload time was churn 9.856 to 10.427 ms, mixed 3.433 to 3.449 ms, and fragmentation 1.201 to 0.991 ms. Churn pause time fell from 0.192 to 0.121 ms and fragmentation pause time from 0.821 to 0.655 ms. The current gen/gsgc comparison is 8.21/10.64 ms churn, 3.65/3.44 ms mixed, and 1.70/1.00 ms fragmentation. Verified the full GSGC suite, 15/15 ASan+UBSan runtime tests, focused exact-pointer validation, and 15/15 semi and gen runtime regressions.
