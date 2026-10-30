---
id: oti-kvb7
status: open
deps: []
links: []
created: 2026-10-30T04:18:17Z
type: task
priority: 2
assignee: Phil
tags: [perf, core]
---
# contains? should stop at the first match

core.ot:474: lists and arrays use for-each with a flag, so it always walks the whole sequence and allocates a closure (26 ms vs 1 us for member at element 0 on a 200k list). Use a member-style equal? loop for lists and find-index for arrays (not member, which copies the array tail via drop).

