---
id: lan-ndec
status: open
deps: []
links: []
created: 2026-10-19T04:58:17Z
type: bug
priority: 2
assignee: Phil
---
# C GC stress failures in ext-demo and conditions tests

Both fail under OTIUM_GC_STRESS=1 on the commit before the build cleanup (57e287e) as well as after; neither test ran under stress before, because the old Makefile only stressed selected checks. Now visible with: just test-stress extension conditions

- extension: build/otium --no-project --path c/tests/modules c/tests/ext-demo.ot prints 'counter-inc!: wrong foreign type' (exit 0, so the test fails on output). Looks like a foreign object or its type tag not surviving a move.
- conditions: tests/test_conditions.py C run aborts with 'live heap exceeds semispace during collection 2816 (copy ~19-22 MB)' while running tests/conditions/common.ot (64 checks) with the semi collector.

Not investigated further.

