---
id: lan-rv8s
status: closed
deps: []
links: []
created: 2026-08-16T00:37:23Z
type: bug
priority: 3
assignee: Phil
tags: [cleanup]
---
# Trivial redundant conditions

arith.cpp:90 nat_div: 'b.i != 0 &&' is redundant, the zero check on the previous line already returned. eval.cpp:677 ns clause check: 'pairp(clause) && clause.tag == Tag::Pair' tests the same thing twice.

