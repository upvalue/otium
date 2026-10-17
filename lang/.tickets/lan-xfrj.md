---
id: lan-xfrj
status: open
deps: []
links: []
created: 2026-10-17T05:16:05Z
type: epic
priority: 2
assignee: Phil
tags: [portability]
---
# Define a shared-code subset for runtime-independent libraries

Libraries meant to run on both runtimes (a future UI library, FFI bindings) need a written list of what they may use, and the small reader/prelude gaps that block that subset need closing. Related to GAPS.txt, but these are the items that matter for new code, not OTCL.

## Acceptance Criteria

A doc lists the subset; each child gap is fixed or has a stated workaround; shared library tests run on both runtimes.

