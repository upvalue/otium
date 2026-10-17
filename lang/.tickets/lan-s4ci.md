---
id: lan-s4ci
status: closed
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

C uses (require '(x :as y)) and .ot; Chez uses (require x :as y) and .ot. Pick one form both accept, or a per-runtime loader adapter as docs/ffi.md allows.

## Acceptance Criteria

One shared module is required with identical source on both runtimes.


## Notes

**2026-10-17T21:45:12Z**

Both runtimes accept shared require forms and search .ot then .scm per directory. shared/tests/test_modules.py verifies search precedence, failures, cycles, reloads, and forwarding callbacks on both runtimes.
