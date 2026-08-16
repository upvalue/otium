---
id: lan-ouqi
status: open
deps: []
links: []
created: 2026-08-16T00:37:23Z
type: chore
priority: 2
assignee: Phil
tags: [cleanup]
---
# Buf should reuse Vec<char>

Buf duplicates Vec<char>'s reserve/push/clear and both move operations verbatim (vec.hpp). Make Buf contain or derive from Vec<char>, adding only append/appendCstr/printf. ~40 LoC. Mind BufferData::buf placement-new/destructor use in heap.cpp when changing.

