---
id: lan-ius2
status: open
deps: []
links: [lan-dm0j, lan-qwff, lan-ab4w, lan-hfil]
created: 2026-08-16T20:28:30Z
type: task
priority: 2
assignee: Phil
tags: [embedded, alloc, compiler]
---
# Compile-scoped arena for the compiler working set

Compilation is the worst per-unit allocation churn in the runtime. Measured with a counting allocator: 200 compiles of a nested lambda produce 3806 allocs and 405 reallocs, roughly 19 allocations per compile.

Sources: one ot_alloc(sizeof(LambdaInfo)) per lambda (src/compile.c:136, 178, 252), four vectors per LambdaInfo node, a Buf and a VecU32 per Compiler, and a VecU32 for exits per cond/case.

Compilation is a bounded phase with a single free point, which is the textbook arena case. Allocate LambdaInfo nodes and the compiler vectors from an arena that is reset rather than freed at the end of each toplevel compile, keeping its block for the next one. lambda_info_free, lambda_info_deinit and the scattered vec_deinit calls all go away.

Two things fall out. Arena reset is exception-safe by construction, so the raise-and-continue leak surface in lan-dm0j stops being reachable through this path. And if the arena is a fixed block on a constrained target, runaway macro expansion fails cleanly with "compiler arena exhausted" instead of taking the device down.

Note the growable vectors need arena-aware growth -- realloc of an arena block is not a thing. Either give the arena a bump-and-copy grow, or size these from the analysis pass which already knows the binding and capture counts before the emit pass runs.

## Acceptance Criteria

Compiler working set allocates from a resettable arena. Per-compile host allocator traffic drops to zero once the arena reaches high-water, confirmed with the counting-allocator probe. No leak on the compile-error path.

