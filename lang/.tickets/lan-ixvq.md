---
id: lan-ixvq
status: open
deps: []
links: []
created: 2026-08-16T15:36:20Z
type: chore
priority: 2
assignee: Phil
tags: [loc, heap, code-quality]
---
# table/array API declared in both heap.hpp and builtins.hpp with independent comments

table_get/table_put/array_get/array_push are declared in heap.hpp:231-234 AND builtins.hpp:83-88, each with its own independently maintained comment block — and those comments ARE the load-bearing alloc-free contract (~30 call sites rely on it). One declaration site should own the contract; the other should include/refer to it. Drift here silently invalidates the GC-safety audit trail.


## Notes

**2026-08-16T15:55:44Z**

Decision (2026-08-16): heap.hpp becomes the single canonical home for the table/array API contract (incl. the alloc-free comments); builtins.hpp refers to it. After the layer collapse (lan-8jpu), also declare table_iter_next there so the printer can call it directly, replacing the weak printer_table_next hook.

**2026-08-16T21:40:22Z**

Resolved: builtins.h no longer redeclares the table/array API; heap.h owns the contract, which is now the opposite of what it said.
