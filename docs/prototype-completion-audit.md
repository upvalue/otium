# Prototype completion audit

This is the evidence checklist for `prototype-completion.md`. The goal remains
open. Passing the current compliance inventory is one gate, not the whole audit.
Do not consult the original C implementation or repository history.

| Requirement | Current evidence | Remaining work |
| --- | --- | --- |
| Core language and applicable public tests | `prototype/compliance.py`: 66 passing groups, two failures, no skips after adding resource, SQLite, diagnostics and CLI | Fix the diagnostic failures; classify the other public definitions below before calling the inventory complete. |
| No generated-C execution or runtime compiler dependency | Bytecode compiler and libffi; `test-no-compiler` and `test-foreign-no-compiler` targets | Repeat after final integration changes and inspect launch paths. |
| Union ABI classification | All 222 unchanged common FFI checks pass normally, under forced collection and ASan/UBSan | Supported union-call target is x86-64 Linux SysV; other targets reject at bind time. |
| Callbacks and explicit ownership | Public callback 51, ownership 10, callback exit/thread groups pass; forced-GC, self-release, retained procedure, metadata replacement and idle-host checks | Keep these in the final sanitizer run; review aggregate calls that invoke callbacks. |
| FFI diagnostics and memory validation | `test_foreign_errors.ot`: 32 standalone checks; full loader messages; shared validation agrees with Chez in five added cases | Recheck final coverage against `specs/ffi.md`, including failure cleanup and cached layouts. |
| Host port I/O and managed buffers | `test_io_host.c`, existing port/file/reader/interrupt/shutdown tests; ASan/UBSan | Port callbacks are implemented. Filesystem metadata and source/module loading still bypass the host table. Resolve that remaining embedding scope. |
| CLI and project behavior | Public CLI library, options, arguments, paths and greeting tests pass; project parsing and preload-only regressions pass | Decide explicit runtime routing and classify remaining launcher services/options. Prototype help is intentionally separate and lists supported options; Chez remains the default. |
| Live reload and retained resources | Foreign reload tests, retained callback/layout tests and 100 callback replacement cycles | Measure long reload cycles with classes, methods, code and resources together. |
| Startup and steady-state performance | Existing benchmark harnesses and historical measurements | Run fresh VM-only measurements, including allocation and retention; measure slot/generic dispatch before deciding whether caches are required. |
| Final validation and milestones | FFI milestones committed; port implementation passed 40 subsystem targets and affected sanitizer checks | Final requirement-by-requirement audit, relevant sanitizers/differentials, ticket update and coherent commits. |

## Public test inventory still to classify

The compliance runner imports `tests/tests.py`, library, modules, reader,
conditions, syntax-quote, FFI, resource, SQLite, diagnostics and CLI definitions.
The added suites exposed cleanup errors discarded during unwinding, missing
process environment/output helpers, diagnostic codes/rendering, and launcher
options/project behavior. Cleanup, process helpers and the public CLI cases are fixed. The two
remaining diagnostic failures stay in the runner.

The diagnostic catalog test is runtime-independent and reads the original C
sources, so it must not execute in this clean-room inventory. Chez-only tests
remain outside the prototype run, including all Manse definitions and the
inactive-backend CLI options test. Objective-C definitions only register on
macOS; this host is Linux. The runtime-independent CLI help parity test still
needs explicit launcher integration coverage.

Other public definitions remain under dev, format, lint, sdl and ui. UI icon and
layout tests require `build/ui-config.ot`; application/demand tests import SDL
and UI modules even where they mock calls. Do not treat those dependencies as
proof that the tests cannot run. Inspect their dependencies and assertions. Record each
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
