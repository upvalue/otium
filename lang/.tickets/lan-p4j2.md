---
id: lan-p4j2
status: in_progress
deps: []
links: [lan-pzh6]
created: 2026-08-16T21:55:15Z
type: task
priority: 1
assignee: Phil
tags: [gc, code-quality, refactor, agents]
---
# Make slots the normal interface to GC values

The moving collector makes a raw heap value stale at the next allocation. The old API exposed raw collection storage and took raw `Value` arguments in allocating mutators, so callers could write a rooting bug without crossing any visible boundary.

Use `src/slots.h` everywhere outside the files that implement the collector-facing machinery. Heap values live in value-stack slots named by `Ref`. Operations take source and destination Refs, return C scalars where appropriate, and do not expose pointers into the heap.

The files allowed to work on heap internals directly are:

- `src/heap.c`
- `src/vm.c`
- `src/slots.c`
- `src/collections.c`

`heap.h` requires an `OT_HEAP_INTERNALS` permit. `tests/check_hygiene.py` owns the same allowlist and rejects heap layout identifiers elsewhere. Focused low-level tests may carry an explicit test-only permit.

The slot API covers collection reads and mutation, rooted sequence and table iteration, string and buffer copy-out, namespace access, reader and printer traversal, code construction and inspection, evaluation, conditions, restarts, params, and foreign objects. `compile.c` uses the same API; it is not an exception.

Keep `vm.c` direct for the dispatch loop. Small numeric and collection primitives may move behind the existing boundary when several slot calls would otherwise sit in a hot native.

Public embedding API design and runtime stale-handle detection are out of scope.

## Acceptance criteria

- `heap.h` fails to compile without a permit.
- Hygiene rejects unauthorized permits, heap includes, and heap layout access; planted violations prove each path.
- Allocating collection mutators take `Ref` for the collection and every heap-valued argument.
- Builtins, extensions, reader, printer, namespaces, evaluator, code handling, and compiler do not take interior heap pointers.
- Legacy Value-facing compatibility shims used during the migration are removed.
- `AGENTS.md` points normal code at `slots.h` and names the four direct-access files.
- The complete normal Meson suite passes, including hygiene, unit, CLI, R7RS port, and extension tests.
