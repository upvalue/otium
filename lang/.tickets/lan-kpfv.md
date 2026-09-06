---
id: lan-kpfv
status: open
deps: []
links: []
created: 2026-09-06T22:39:19Z
type: task
priority: 2
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Factor kind-preserving sequence construction in the prelude

map, filter, concat, take, drop, sort-by, to-list, and list-to-arr repeat array accumulation and list restoration logic. The private conversions are also weaker than the documented public conversions.

## Acceptance Criteria

A small collection-builder or finish-like abstraction removes repeated kind branching; strict public list and array conversions are reused; nil and improper-list behavior remains explicit and tested.

