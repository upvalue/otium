---
id: oti-k5zm
status: open
deps: []
links: []
created: 2026-10-30T04:18:16Z
type: task
priority: 2
assignee: Phil
tags: [perf, runtime]
---
# Sequential string indexing is quadratic

Strings are UTF-8 and string-ref/get/substring scan from the start (support.c:361, strings.c:20), so loops over strings are O(n^2): JSON parse/encode (110 KB takes 4.8 s), for over a string (loop.ot:44), compile --notes (tools/compile.ot:16). Proposal: a one-entry cache on ot_state (string, char index, byte offset), continued from when the next lookup is on the same string at or after the cached index; cleared at each collection. Strings are immutable so nothing else invalidates it. Native code reaches it as it is: string-ref is :direct in known.ot, so native calls the C builtin with rt. No API change, no per-string memory. See also oti-tqwb (substring).

