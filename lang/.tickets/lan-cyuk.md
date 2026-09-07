---
id: lan-cyuk
status: closed
deps: []
links: []
created: 2026-09-06T22:39:00Z
type: task
priority: 1
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Resolve array equality contract disagreement

The specification says equal? compares arrays structurally, while the implementation and conformance test deliberately compare distinct arrays by identity. Collection operations such as contains? inherit the disagreement.

## Acceptance Criteria

One semantic choice is documented consistently in the specification, implementation, and tests; nested arrays and mutable arrays used as table keys have explicit coverage.


## Notes

**2026-09-06T23:05:10Z**

Semantic decision: arrays use stable identity for equal?, sequence membership, and table keys because arrays are mutable; the specification and regression coverage now agree.
