---
id: oti-2t6x
status: open
deps: []
links: []
created: 2026-10-30T02:22:29Z
type: bug
priority: 1
assignee: Phil
tags: [native, memory]
---
# Reuse or unmap dead native code

oti_code_space_free (c/src/native.c:525) does nothing; the code space only bump-allocates from 4 MB mmap chunks. Reclaimed code frees its metadata (free_code) but the machine code stays, so every live reload leaks code until exit. Options: free-list dead ranges in a chunk and unmap empty chunks, or count live bytes per chunk and unmap at zero (may want smaller chunks). Reused pages go through the RW/RX mprotect flip; check arm64 icache flushing covers reused pages.

## Acceptance Criteria

Reloading the same file repeatedly keeps code space bounded.

