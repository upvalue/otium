---
id: lan-nzlc
status: open
deps: []
links: []
created: 2026-08-15T23:20:32Z
type: bug
priority: 2
assignee: Phil
---
# merge: nil values in later tables should delete keys

Conformance 04-tables fails: (merge {:a 1 :b 2} nil {:a nil :c 3}) should drop :a (nil value deletes, per spec table semantics) and skip nil table arguments. Current prelude merge keeps :a 1. Expected keys [:b :c], values [2 3]; got [:a :b :c] / [1 2 3]. Fix is in prelude/prelude.scm's merge.

## Acceptance Criteria

tests/otium/run-tests.py: 04-tables passes

