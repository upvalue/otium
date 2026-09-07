---
id: lan-ghbl
status: closed
deps: []
links: []
created: 2026-09-06T22:39:00Z
type: bug
priority: 2
assignee: Phil
parent: lan-xh00
tags: [code-quality-2026-09-06]
---
# Enforce cond clause shape and else requirements

The compiler accepts an else clause with no body and returns true, despite the language contract that else requires a body. Clause-shape validation is distributed inside bytecode emission.

## Acceptance Criteria

cond rejects an empty else body and other malformed clauses before execution; valid test-only clauses, if retained, are explicitly documented and tested.

