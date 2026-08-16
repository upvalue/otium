---
id: lan-1bf0
status: open
deps: []
links: [lan-ab4w]
created: 2026-08-16T20:27:24Z
type: task
priority: 2
assignee: Phil
tags: [embedded, alloc, intern]
---
# Intern names into an append-only byte arena

intern_id does one ot_alloc(len+1) per distinct symbol (src/intern.c:51) and never frees it until intern_deinit. Names are immortal by design, which makes the per-name allocation pure overhead -- an append-only byte arena is the exact fit.

InternName becomes (offset, len) into the arena instead of an owned char*. One block allocation per arena chunk instead of one per symbol, and the memcmp probe loop in intern_id gets better locality as a side effect since the names it compares end up adjacent.

Small and self-contained. The only care needed is that intern_name returns a NUL-terminated pointer today and callers rely on that (state.c snprintf paths, error formatting), so keep the trailing NUL when appending.

## Acceptance Criteria

One allocation per arena chunk rather than per interned name. intern_name still returns a NUL-terminated pointer valid for the life of the Intern.

