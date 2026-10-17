---
id: lan-23n7
status: open
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

