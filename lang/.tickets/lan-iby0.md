---
id: lan-iby0
status: open
deps: []
links: []
created: 2026-09-06T22:38:41Z
type: bug
priority: 0
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Compare large integers without double precision loss

Numeric comparison converts every integer to double. Adjacent integers above 2^53 can compare equal, order incorrectly, and make min or max return the wrong operand.

## Acceptance Criteria

Int versus int comparisons are exact; mixed int and float comparisons are boundary-aware and satisfy the documented numeric semantics; regression tests cover values around 2^53 and fixnum limits.

