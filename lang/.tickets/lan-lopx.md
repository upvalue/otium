---
id: lan-lopx
status: open
deps: []
links: []
created: 2026-10-21T00:40:41Z
type: feature
priority: 1
assignee: Phil
tags: [io, manse]
---
# Implement specs/io.md

Implement the I/O spec in the order its 'Order of work' section gives, committing after each step. Characters and bytevectors (steps 1-2) are wanted first: manse needs integer->char and string-from-chars for terminal cell text, and the JSON example needs them.

## Design

The spec is a draft. Its five Open questions, and any new contract question found while implementing, go to Phil before being decided; do not resolve them by fiat. Steps that don't depend on an open question can proceed meanwhile.

## Acceptance Criteria

Each step has tests on both runtimes (C may lag only where the spec allows). Existing suites pass: just test c chez shared. specs/io.md updated to say what is implemented; stale statements in docs/portability.md deleted.

