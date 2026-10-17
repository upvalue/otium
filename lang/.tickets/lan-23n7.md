---
id: lan-23n7
status: closed
deps: []
links: []
created: 2026-10-17T05:16:06Z
type: task
priority: 2
assignee: Phil
parent: lan-xfrj
tags: [portability, design]
---
# Agree on let binding semantics

C let is sequential and C has no let*; Chez let is parallel. Shared code currently cannot use dependent bindings. Options: add let* to C as an alias, or change one runtime's let. Language decision; discuss first.

## Acceptance Criteria

Decision recorded; shared code has a form with the same meaning on both runtimes.


## Notes

**2026-10-17T21:45:12Z**

Both runtimes use sequential let bindings through the shared expander. Documented in docs/portability.md and covered by shared/tests/expander/checks.scm.
