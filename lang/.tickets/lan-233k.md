---
id: lan-233k
status: open
deps: []
links: []
created: 2026-08-16T21:55:08Z
type: chore
priority: 3
assignee: Phil
tags: [c, valgrind, cleanup]
---
# Free CLI load-path allocations at shutdown

Valgrind reports 66 to 151 bytes still reachable after otherwise clean CLI runs. The allocations are duplicated load-path strings plus the backing storage for g_loadPath. main destroys the VM and options.files, but never frees the strings owned by g_loadPath or deinitializes that vector. Runs covering a file, server mode, the demo extension, and a small recursive program had zero Memcheck errors and no definitely, indirectly, or possibly lost blocks.

## Design

Add one cleanup path for CLI-owned storage. Free every string in g_loadPath, deinitialize the vector, and use the cleanup path for normal shutdown and returns that can happen after argument parsing has added a path. Keep process behavior and load-path precedence unchanged.

## Acceptance Criteria

Representative file, server, and --path runs finish with no still-reachable blocks attributed to dup_cstr or g_loadPath under Valgrind. Paths added by --path, OTIUM_PATH, project.ot, and the default dot entry are all freed. Error exits after accepting a path also free any owned CLI storage. The existing CLI test suite still passes.
