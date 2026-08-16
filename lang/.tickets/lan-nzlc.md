---
id: lan-nzlc
status: open
deps: []
links: [lan-t86k]
created: 2026-08-15T23:20:32Z
type: bug
priority: 2
assignee: Phil
---
# merge: nil values in later tables should delete keys

Conformance 04-tables fails: (merge {:a 1 :b 2} nil {:a nil :c 3}) should drop :a (nil value deletes, per spec table semantics) and skip nil table arguments. Current prelude merge keeps :a 1. Expected keys [:b :c], values [2 3]; got [:a :b :c] / [1 2 3]. Fix is in prelude/prelude.scm's merge.

## Acceptance Criteria

tests/otium/run-tests.py: 04-tables passes


## Notes

**2026-08-16T00:22:50Z**

Analysis 2026-08-15 (see lan-t86k, filed independently, now linked): the fix cannot live in prelude merge — spec 2.2 makes 'present with nil' unrepresentable, so {:a nil :c 3} evaluates to {:c 3} before merge sees it; the deletion is unreachable no matter how merge iterates. Either the test/expected changes, or the spec grows a construct that carries key->nil to merge. Design question for Phil.
