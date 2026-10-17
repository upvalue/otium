---
id: lan-ngkx
status: open
deps: []
links: []
created: 2026-10-17T05:16:07Z
type: task
priority: 3
assignee: Phil
parent: lan-xfrj
tags: [portability, design]
---
# Common mutable indexed collection for shared code

C has arrays, Chez has vectors, with no common name or operations. Options: shared shim names, or typed maps over FFI memory.

## Acceptance Criteria

Shared code can create, index, set and grow an indexed collection on both runtimes.

