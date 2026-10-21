---
id: lan-giys
status: open
deps: []
links: [lan-cck8]
created: 2026-10-21T22:25:08Z
type: task
priority: 2
assignee: Phil
tags: [oct-21-cq, ffi, perf]
---
# Bulk byte copies between bytevectors and FFI memory

Byte data moves one FFI call per byte: lib/otium/sqlite.ot:310 and :375, manse/pty.ot write-bytes!, ghostty.ot:268 and :276, and the NUL scans in pty/write! and ghostty/write!. Add ffi/read-bytes and ffi/write-bytes! (bytevector <-> memory), a byte length from alloc-utf8, and a length-bounded read-utf8 (removes read-gstring's copy-and-terminate).


## Notes

**2026-10-21T22:51:22Z**

Strawman for discussion: add (ffi/read-bytes pointer count) and (ffi/write-bytes! pointer bytes [start end]); expose UTF-8 byte length without changing alloc-utf8’s pointer return. Existing read-utf8’s second argument is a NUL-search bound, so an exact-length decoder needs a separate spelling or explicit mode to avoid silently changing callers. Proposed spelling: read-utf8-bytes. Confirm that API and whether alloc-utf8 should gain a companion returning pointer plus length before implementing the migration.
