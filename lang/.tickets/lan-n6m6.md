---
id: lan-n6m6
status: closed
deps: []
links: []
created: 2026-10-17T21:24:28Z
type: task
priority: 2
assignee: Phil
---
# Start classes with assignment through calls

Implement the assignment foundation from docs/classes.md on C and Chez: (set! (object key) value) and set-applied! for tables and arrays. Preserve single evaluation in object, key, value order and return the assigned value. Class instances and slot dispatch follow in a later slice.

## Acceptance Criteria

Shared expander tests pass on C, C with GC stress, and Chez; runtime regression suites pass. No README changes.


## Notes

**2026-10-17T21:25:26Z**

Implemented in experiment/classes at /private/tmp/otium-classes/lang. Full make test passed with an isolated BUILD_ROOT. All 74 shared expander checks pass on C, C with GC stress, and Chez, including heap value identity. make gc-lint completed with two warnings in untouched VM code at otium.c:3338 and :3398. Generated documentation and git diff --check pass. Next slice: class descriptors and fixed slots on both runtimes, then ancestor dispatch and reload migration.
