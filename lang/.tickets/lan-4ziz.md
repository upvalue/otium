---
id: lan-4ziz
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
# Implement and inventory every documented core binding

The specification promises defn, caar, cadr, cddr, list-to-array, array-to-list, current-ns, and describe, but the current runtime and prelude do not bind them. Existing tests do not inventory the public namespace.

## Acceptance Criteria

All documented bindings exist with the specified behavior and strictness; a generated or table-driven conformance check fails whenever the specification inventory and exported core namespace drift.

