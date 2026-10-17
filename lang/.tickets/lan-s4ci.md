---
id: lan-s4ci
status: open
deps: []
links: []
created: 2026-10-17T05:16:06Z
type: task
priority: 2
assignee: Phil
parent: lan-xfrj
tags: [portability]
---
# Common require form and file suffix for shared modules

C uses (require '(x :as y)) and .ot; Chez uses (require x :as y) and .scm. Pick one form both accept, or a per-runtime loader adapter as docs/ffi.md allows.

## Acceptance Criteria

One shared module is required with identical source on both runtimes.

