---
id: lan-4gxf
status: closed
deps: []
links: []
created: 2026-09-06T22:38:41Z
type: bug
priority: 0
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Reject pair mutations that introduce cycles

set-car! and set-cdr! only check frozen keys before storing. Cyclic cdr chains are accepted despite the specification and can hang printing or overflow C recursion during hashing and equality.

## Acceptance Criteria

Both mutation operations reject any value graph that would make the target pair reachable from itself; errors are catchable; printing, equality, and table-key regression tests cannot be made to hang through pair mutation.

