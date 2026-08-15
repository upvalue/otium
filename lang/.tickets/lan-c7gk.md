---
id: lan-c7gk
status: open
deps: []
links: [lan-6mpt]
created: 2026-08-15T23:20:48Z
type: bug
priority: 1
assignee: Phil
---
# Finish GC-staleness sweep under gc_stress + ASan

The semispace GC moves everything on collect; any C++ local holding a heap Value across an allocating call goes stale. A large sweep of eval.cpp/vm.cpp/ns.cpp/data.cpp is done and verified (build with meson setup build-stress -Dgc_stress=true -Db_sanitize=address; from-space is poisoned 0xAB in stress mode so stale reads fail loudly). Conformance under the stress binary is at 5/10. Known remaining offenders from ASan traces: (1) nat_invoke_restart reads restart_data(target) after list_from_stack allocates (eval.cpp ~1112); (2) make_string_h memcpys from the source pointer after alloc, so building a string from another heap string's bytes (nat_substring, likely others in string.cpp) is a use-after-free when the source moves — root the source or copy via a C buffer first; (3) unaudited: string.cpp, arith.cpp, sys.cpp, printer table path, reader. Also: 06-tco times out under stress (collect-per-alloc is quadratic on 100k-iteration loops) — needs a longer timeout or a stress-collect throttle; and the doctest files themselves hold unrooted locals across allocs, so the full doctest suite is not stress-clean by design — decide whether to fix the tests or only run scm conformance under stress.

## Acceptance Criteria

tests/otium/run-tests.py against the gc_stress+ASan binary: 10/10 (with a suitable timeout for 06-tco)

