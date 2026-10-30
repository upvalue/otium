---
id: oti-tr6n
status: open
deps: []
links: [oti-kj2x]
created: 2026-10-30T02:22:31Z
type: task
priority: 4
assignee: Phil
tags: [threads, memory]
---
# Keep thread mailboxes on the GC heap

t->mailbox (c/src/threads.c:84) is a realloc'd ot_value array visited by visit_thread. As a heap array it would count against the heap limit and drop a loop from visit_thread. Low value on its own; may be subsumed by host memory accounting.

## Acceptance Criteria

Mailboxes are heap arrays, or this is closed as covered by host accounting.

