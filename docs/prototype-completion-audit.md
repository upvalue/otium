# Prototype completion audit

This is the evidence checklist for `prototype-completion.md`. The goal remains
open. Passing the current compliance inventory is one gate, not the whole audit.
Do not consult the original C implementation or repository history.

| Requirement | Current evidence | Remaining work |
| --- | --- | --- |
| Core language and applicable public tests | `prototype/compliance.py`: 56 passing core/FFI groups, no skips | Classify every other public test definition below before calling the inventory complete. |
| No generated-C execution or runtime compiler dependency | Bytecode compiler and libffi; `test-no-compiler` and `test-foreign-no-compiler` targets | Repeat after final integration changes and inspect launch paths. |
| Union ABI classification | All 222 unchanged common FFI checks pass normally, under forced collection and ASan/UBSan | Supported union-call target is x86-64 Linux SysV; other targets reject at bind time. |
| Callbacks and explicit ownership | Public callback 51, ownership 10, callback exit/thread groups pass; forced-GC, self-release, retained procedure, metadata replacement and idle-host checks | Keep these in the final sanitizer run; review aggregate calls that invoke callbacks. |
| FFI diagnostics and memory validation | `test_foreign_errors.ot`: 32 standalone checks; full loader messages; shared validation agrees with Chez in five added cases | Recheck final coverage against `specs/ffi.md`, including failure cleanup and cached layouts. |
| Host port I/O and managed buffers | `test_io_host.c`, existing port/file/reader/interrupt/shutdown tests; ASan/UBSan | Port callbacks are implemented. Filesystem metadata and source/module loading still bypass the host table. Resolve that remaining embedding scope. |
| CLI and project behavior | Existing prototype CLI tests; new REPL/program stdin sharing and output ordering tests | Inventory public CLI/project contracts, record routing decisions and verify explicit runtime selection. Chez remains the default. |
| Live reload and retained resources | Foreign reload tests, retained callback/layout tests and 100 callback replacement cycles | Measure long reload cycles with classes, methods, code and resources together. |
| Startup and steady-state performance | Existing benchmark harnesses and historical measurements | Run fresh VM-only measurements, including allocation and retention; measure slot/generic dispatch before deciding whether caches are required. |
| Final validation and milestones | FFI milestones committed; port implementation passed 40 subsystem targets and affected sanitizer checks | Final requirement-by-requirement audit, relevant sanitizers/differentials, ticket update and coherent commits. |

## Public test inventory still to classify

The compliance runner imports `tests/tests.py`, library, modules, reader,
conditions, syntax-quote and FFI definitions. Other public definitions currently
exist under cli, dev, diagnostics, format, lint, manse, objc, resource, sdl, sqlite
and ui. Inspect their declared runtimes, dependencies and assertions. Record each
applicable group and run it, or document a concrete reason it belongs to a
separate host/tooling/platform contract. Do not exclude a failing runtime test
merely because its launcher or fixture setup differs.

## Validation limits

ASan/UBSan runs use `ASAN_OPTIONS=detect_leaks=0`; they do not establish absence of
native leaks. Explicit allocation counters and bounded metadata tests provide
separate retention evidence. Optional LLVM development tools are unavailable for
GC lint. Standalone collector targets are omitted while working on ports and FFI,
per the repository instruction; subsystem regressions still exercise moving
collection at allocation boundaries.

The root completion brief remains edited in the worktree. The documentation hook
rejects root-level documentation changes, so milestones commit source, tests,
this audit and `docs/runtime-rewrite.md` while retaining the brief's current state.
