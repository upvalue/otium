---
id: lan-t86k
status: closed
deps: []
links: [lan-nzlc]
created: 2026-08-16T00:17:46Z
type: bug
priority: 2
assignee: Phil
---
# merge nil-value deletion is unreachable: spec 2.2 vs 04-tables expectation

tests/otium/04-tables.scm expects (merge {:a 1 :b 2} nil {:a nil :c 3}) => keys [:b :c], i.e. the :a nil entry deletes :a. But spec 2.2 makes 'present with nil' unrepresentable: the {...} constructor drops :a before merge ever sees it, so the deletion can never fire. Spec 10 (merge row: 'nil values delete') is vacuous under 2.2. Either (a) the expected file / test is wrong and merge-with-nil-value deletion is dropped from the spec's merge row, or (b) some construct must carry key->nil pairs to merge (spec change). Currently the only red conformance test (9/10). DESIGN QUESTION - discuss before resolving.


## Notes

**2026-08-16T00:22:51Z**

Duplicate of lan-nzlc (older); analysis copied there. Closing this one.
