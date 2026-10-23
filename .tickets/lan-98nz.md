---
id: lan-98nz
status: closed
deps: []
links: []
created: 2026-10-17T21:26:32Z
type: feature
priority: 2
assignee: Phil
---
# Implement classes and generic method combination on both runtimes

Implement specs/classes.md in full: final classes with fixed slots, abstract parent chains, callable slot access and assignment, defaults/accessors/with-slots, ancestor dispatch, call-next-method and around methods, lazy instance migration, reload identity, slot and dispatch caches. Replace typed maps, update examples and benchmarks, test both runtimes and merge when complete.


## Notes

**2026-10-17T21:50:49Z**

Implemented fixed-slot classes, abstract ancestry, method combination, reload migration, applied assignment, and slot/dispatch caches on C and Chez. Replaced typed maps and migrated examples. Validation: all make test targets passed (the migrated animal example was corrected after the first integration run); 96 shared class checks on C, C GC stress, Chez, generational GC stress, and WASM; 527 C oracle/namespace checks and 583 Chez checks; site tests and shared class benchmark on both runtimes. GC lint completed with only the two existing VM stale-borrow warnings.
