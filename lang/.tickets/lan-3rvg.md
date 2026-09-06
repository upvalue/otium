---
id: lan-3rvg
status: open
deps: []
links: []
created: 2026-09-06T22:38:41Z
type: bug
priority: 1
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Require every append argument to be a proper list

append validates all arguments except the final one, allowing an improper result such as append of a proper prefix and scalar tail despite the proper-list contract.

## Acceptance Criteria

Every append argument is validated as a proper list and invalid final arguments raise a catchable error; quasiquote splicing retains its intended dotted-tail behavior through its separate primitive.

