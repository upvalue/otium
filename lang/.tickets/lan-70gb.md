---
id: lan-70gb
status: in_progress
deps: []
links: [lan-p1e2, lan-iuyk]
created: 2026-08-16T02:08:43Z
type: epic
priority: 1
assignee: Phil
tags: [benchmarks, performance, stdlib]
---
# Port a representative set of r7rs-benchmarks to Otium

Port a representative set of ecraven/r7rs-benchmarks to Otium and keep enough provenance and measurement discipline to compare Otium with Scheme implementations on the same workloads.

This is a benchmark-porting project, not an R7RS compatibility project. The Otium sources should use Otium syntax and APIs. Extend Otium where the missing operation is generally useful, such as clocks, numeric functions, or configurable runtime limits. Do not add compatibility machinery solely to avoid changing an upstream benchmark.

The first target is 12 benchmarks:

- calls and recursion: fib, ack, tak, cpstak
- lists: takl, nqueens, deriv
- allocation and mutation: destruc, mperm, gcbench
- arrays and floating point: quicksort, fft

## Source and port layout

Keep the upstream repository as a git submodule under `benchmarks/r7rs/vendor/`, pinned to commit `85f6acdc4cc4e2b857f307ba56bd0ba931dcccd1`. Record the repository URL and commit in `PROVENANCE.md`. Do not put Otium-specific changes in the submodule.

Put Otium ports under `benchmarks/r7rs/ports/`. Shared Otium benchmark code belongs under `benchmarks/r7rs/lib/`; it is a benchmark API, not an R7RS prelude.

`benchmarks/r7rs/run.py` should:

- use the canonical upstream `.input` file for each benchmark
- generate an Otium program containing those input datums, the shared benchmark driver, and the selected port
- run the requested Otium binary with a per-run timeout
- validate the benchmark's result before accepting a timing
- preserve the upstream parameterized benchmark name
- emit the familiar `+!CSVLINE!+implementation,name,seconds` line
- record raw samples and whole-process wall time as well as the in-language time
- write `results.csv` and `STATUS.md`

Do not compare Otium timings with historical results collected on another machine. The runner should make it possible to run upstream implementations on the same host. Record implementation version, upstream commit, Otium commit, platform, runtime flags, input identity, and whether the time was reported in-language or measured by the harness.

## Porting rules

Preserve the algorithm, canonical input, iteration count, setup/timing boundary, and expected result. Rename operations and translate syntax directly: vectors may become arrays, named let may become explicit recursion, and do loops may become while loops.

Give each port a parity grade in `manifest.json`:

- A: mechanical syntax or name translation
- B: equivalent representation change, such as Scheme vector to Otium array
- C: material semantic adaptation; useful as an Otium benchmark but not a direct cross-implementation comparison
- D: unsupported or replaced algorithm

Document every B or C deviation. Do not translate a benchmark of a language feature into a benchmark of a different feature and present the result as comparable. In particular, continuations rewritten as conditions and built-in equality replaced by a benchmark-local algorithm are C ports.

The initial 12 should be A or B. If one cannot be ported at A/B, mark it honestly and replace it in the initial comparison set rather than weakening the grading rule.

## Otium extensions

Add generally useful primitives as the ports require them. The expected first set is:

- numeric functions: sqrt, exp, log, sin, cos, tan, asin, acos, one- and two-argument atan, expt, truncate
- numeric conversions and predicates: exact, inexact, exact?, inexact?, integer?, nan?, infinite?, finite?
- clocks: current-jiffy using a monotonic nanosecond clock, jiffies-per-second returning 1,000,000,000, and current-second returning wall-clock seconds
- CLI limits: --max-depth, --stack-slots, --heap-init, and --heap-max

Keep the existing defaults. Heap maximum must remain 64 MiB unless overridden. Validate CLI values and report bad values as usage errors.

Add primitives because they fit Otium, not because they happen to have an R7RS name. The names above are already useful and conventional.

## Results

Store enough information to reproduce a result. At minimum:

```text
implementation,benchmark,variant,parity,input,reported_seconds,wall_seconds,status
```

Use `variant=port` for Otium and `variant=upstream` for an unmodified Scheme run. Charts and summaries must label ported results as ported.

Timeouts and crashes are results. An incorrect answer is never a timing result.

## Acceptance criteria

1. The upstream repository is an unmodified submodule pinned to the recorded commit, and a normal recursive clone or `git submodule update --init` supplies it.
2. All 12 initial Otium ports pass reduced-input correctness checks and can be launched with the canonical upstream inputs unchanged. Canonical timeouts and crashes are recorded as results rather than treated as correctness failures.
3. Every port has a reviewed parity grade and documented deviations.
4. Completed runs emit a correct `+!CSVLINE!+otium-...,<benchmark>:<input>,<seconds>` line and record both reported and wall-clock time. Timed-out runs record the timeout without fabricating a timing result.
5. `results.csv` and `STATUS.md` distinguish ok, incorrect, crashed, timeout, and unsupported results.
6. At least one upstream Scheme implementation can be run through the comparison workflow on the same host; unavailable executables are reported as unsupported, not as failed benchmarks.
7. New Otium primitives and CLI flags have doctest and CLI coverage. The normal Meson suite and focused benchmark-port regression tests pass.
8. `NOTES.md` records porting friction and follow-up language ideas without turning benchmark-specific workarounds into core features.
9. `agent-docs/r7rs-benchmarks.md` explains the layout, provenance rules, normal test workflow, comparison model, result fields, and how to add another port.
