---
id: lan-ab4w
status: open
deps: []
links: [lan-qwff, lan-ius2, lan-hfil, lan-1bf0]
created: 2026-08-16T20:27:37Z
type: task
priority: 1
assignee: Phil
tags: [embedded, alloc, builtins]
---
# Per-VM scratch arena for transient Bufs

Every transient Buf in the runtime is a malloc/free pair per operation. Measured with a counting allocator: 2000 calls to (str "x" n) produce 2006 allocs and 2005 frees. That is one round trip through the host allocator per string operation, and on an embedded target it is the churn most likely to fragment.

Sites, all strictly LIFO and all dead before the function returns: seven Bufs in src/builtins/string.c, four in src/builtins/sys.c, readString at src/reader.c:148, the printer output paths, error formatting at src/state.c:47, and the verifier scratch at src/code.c:74.

Pattern: a scratch arena on State with mark/release. scratch_mark returns the current offset, scratch_release rewinds to it. Nesting works because the usage is a stack. The arena grows to high-water once and then never calls the allocator again. make_string_buf already copies out into the GC heap before anything is released, so there is no lifetime subtlety to work through.

Worth noting the measured sizes are tiny -- 16416 bytes across 2000 str calls, about 8 bytes each. A small inline array inside Buf that spills to the heap only when it overflows would erase nearly the same traffic for less work. The arena is still the better answer because it covers the reader and printer paths uniformly and gives a single place to bound scratch memory, but the inline-buffer option is a reasonable fallback if the arena turns out to be invasive.

## Acceptance Criteria

Transient Buf sites allocate from the scratch arena. Repeated string builtin calls show no host allocator traffic after the arena reaches high-water, confirmed with the counting-allocator probe.

