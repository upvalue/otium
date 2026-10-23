---
id: lan-282o
status: closed
deps: []
links: []
created: 2026-10-19T04:24:43Z
type: chore
priority: 2
assignee: Phil
---
# Update AGENTS.md after build/test/layout cleanup

Phil to edit by hand (agents may not edit AGENTS.md). Once the Meson/Just, meson test, and lib/ + native/ changes land, these AGENTS.md statements go stale:

- 'run make gc-lint' -> just gc-lint (runner may stay c/tools/gc-lint/run, now reads build/compile_commands.json)
- 'Avoid running GC tests' -> stress is the opt-in gc-stress test setup (just test-stress); worth saying so
- 'c/ owns the C runtime, build, tests' -> build is the root meson.build
- 'shared/ owns runtime-independent native components' -> lib/ (all Otium source, default search path) and native/ (C/C++ bridges)
- 'The native collector harness stays in c/benchmarks/' -> wherever gc_bench.c lands (planned c/bench/)
- 'Root make test ... make test-c, make test-chez, make test-repl, make test-sdl' -> just test [suite] / meson test --suite c|chez|shared|native|gpu|bench. Note make test-repl never existed.
- Runtime layout paragraph mentions benchmarks/r7rs layout; recheck after benchmark doc/script merge.
- wasm build deleted (decided 2026-10-18); nothing in AGENTS.md mentions it, just confirm.


## Notes

**2026-10-19T04:25:29Z**

Also from 2026-10-18 discussion: AGENTS.md should state the doc policy (delete out-of-date docs rather than update them; docs only in the allowed locations, enforced by ./otium check). AGENTS.md references GAPS.txt, which the plan folds into docs/portability.md.

**2026-10-19T04:52:13Z**

Build/test cleanup landed (uncommitted as of 2026-10-18). Concrete AGENTS.md lines to change: gc-lint is 'just gc-lint'; GC stress is 'just test-stress' (meson test --setup gc-stress), never in the default run; 'c/ owns ... build' -> root meson.build + Justfile; shared/ -> lib/ (Otium source, always on the search path), native/ (C/C++), tests/ (cross-runtime tests); native collector harness is c/bench/gc_bench.c; 'make test-*' -> 'just test [suite]' with suites c, chez, shared, native, gpu, bench; GAPS.txt folded into docs/portability.md (Known differences). The commit hook needs 'just setup' once per clone, so agents should be told to run it. Doc policy: delete stale docs; allowed locations are listed in tools/check-docs.
