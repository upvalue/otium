---
id: lan-bd6c
status: open
deps: []
links: []
created: 2026-09-06T22:39:00Z
type: bug
priority: 2
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Render floats in shortest round-trip form

Float representation uses percent .17g, so common values such as 0.1 print as 0.10000000000000001 despite the shortest-round-trip contract.

## Acceptance Criteria

Finite floats render with the shortest text that reads back to the same value, integral floats retain a decimal marker, and NaN and infinity behavior is specified and tested.

