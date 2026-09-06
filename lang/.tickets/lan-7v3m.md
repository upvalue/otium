---
id: lan-7v3m
status: open
deps: []
links: []
created: 2026-09-06T22:39:00Z
type: bug
priority: 1
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Make require reload discard stale namespace state

The reload path evaluates again into the existing namespace and does not clear old vars. Definitions removed from a module source remain available after reload, contrary to discard-and-reload semantics.

## Acceptance Criteria

Reload removes or replaces definitions from the prior loaded copy without damaging core refers or caller aliases; tests cover removed, changed, private, and failed reload definitions.

