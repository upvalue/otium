---
id: lan-lopx
status: closed
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


## Notes

**2026-10-21T15:44:34Z**

Completed steps 1–6 on both runtimes. Blocking interruptible ports, resource scopes, CPointer ownership, port-first display-to/write-to, overloaded read-string, custom ports, and filesystem operations are implemented. Unified otium.resource and SQLite with core close/with. Finalizers remain deferred; streaming JSON and benchmarking remain separate work. Validation: 161 core/shared tests passed, 2 skipped; all 16 FFI/SQLite tests passed; lint, documentation checks, and GC lint passed.
