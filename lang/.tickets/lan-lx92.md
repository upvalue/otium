---
id: lan-lx92
status: closed
deps: []
links: [lan-ngkx, lan-ug9e]
created: 2026-10-17T15:07:18Z
type: feature
priority: 2
assignee: Phil
---
# Implement the common Otium reader on Chez and C

Implement docs/reader-spec.md. Keep native Chez implementation libraries on their host reader; route Otium input through the common reader contract. Include required array/table/keyword/nil support and shared fixtures. Formatter and linter are out of scope.

## Acceptance Criteria

Shared reader fixtures pass on both runtimes; source loading, require, REPL, and read-string use the agreed syntax; relevant runtime and REPL tests pass; no bootstrap compiler required.


## Notes

**2026-10-17T15:23:01Z**

Implemented the common reader in ordinary Chez Scheme, with Otium file/module/REPL/read-string boundaries and native implementation readers preserved. Added growable arrays, ordered tables with structural frozen pair keys, interned keywords, falsy void/nil, direct hygienic truth checks, predicate adapters and Otium printing. C now shares numeric/string conventions, treats bare true/false as symbols, rejects CLI shebangs, and copies read-string source bytes out of the moving heap (a failure found by the new GC-stress reader fixtures).

Validation: make test passed; make test-reader passes normal and GC-stress fixtures (36 positive/48 negative plus collection, macro, loading and REPL checks); reader GC stress also passed with GC=gen, then the default build was restored. make test-chez passed after the final condition-presence check. make test-ffi passed, including C GC stress. make test-sdl passed headless checks on both runtimes; accelerated checks skipped because the sandbox has no display backend. make gc-lint completed with two warnings in unchanged VM code (code/function across resolve_var), none in reader code. Three existing benchmark workloads passed; raw results are in benchmarks/results-reader.json, with no before/after performance claim.

Updated GAPS.txt, Chez notes, generic-dispatch docs and the runtime reference. Detailed outcomes and remaining library/native-boundary differences are in docs/reader-implementation.md. README.md, reader-spec.md and the handoff links were preserved. Formatter/linter work remains deferred.
