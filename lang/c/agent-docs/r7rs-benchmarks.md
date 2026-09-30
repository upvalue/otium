# r7rs-benchmarks setup

Otium keeps native ports of selected programs from
[ecraven/r7rs-benchmarks](https://github.com/ecraven/r7rs-benchmarks) under
`benchmarks/r7rs/`. This is a performance suite, not an R7RS compatibility
layer. The ports use Otium syntax and APIs while retaining the upstream
algorithm, input, timing boundary, and result check where possible.

## Layout

- `vendor/` is the pinned upstream git submodule. `PROVENANCE.md` records its
  repository and commit.
- `ports/` contains the Otium translations.
- `lib/benchmark.scm` supplies the Otium benchmark driver.
- `manifest.json` lists the supported ports, parity grades, deviations, and
  exceptional heap limits.
- `run.py` runs Otium and, optionally, an upstream Scheme implementation on the
  same machine.
- `results.csv` contains raw samples. `STATUS.md` is the corresponding readable
  summary.
- `test_ports.py` runs reduced-input correctness checks for every port.

Do not edit or commit changes inside `vendor/`. Put an Otium-specific change in
`ports/` or `lib/`, then describe any semantic difference in `manifest.json`.

## Building and running

From `lang/`, build Otium normally:

```sh
git submodule update --init benchmarks/r7rs/vendor
make
```

Cloning the parent repository with `--recurse-submodules` performs the first
step automatically.

Run one or more canonical inputs:

```sh
python3 benchmarks/r7rs/run.py build/otium fib ack
```

Omitting benchmark names runs every port. `--runs N` collects multiple samples,
and `--timeout SECONDS` sets a per-sample limit. The default is one sample with a
300-second timeout.

If Guile is installed, the same command can also run the unmodified upstream
program on the same host:

```sh
python3 benchmarks/r7rs/run.py build/otium fib --reference guile
```

Otium receives the canonical input datums in its generated source. Guile
receives the same vendored input on standard input. The runner overwrites
`results.csv` and `STATUS.md`, so save a result elsewhere before starting a new
campaign if it matters.

The runner gives benchmark processes larger evaluator and stack limits than the
CLI defaults. `manifest.json` raises the heap cap for ports with unusually large
live sets. These limits are recorded in each result row.

## Correctness checks

Run the normal suite:

```sh
python3 benchmarks/r7rs/test_ports.py build/otium
```

The `r7rs-ports` test exercises all ports with reduced inputs and checks their
reported answers. When Guile is available, it also checks the upstream-source
assembly path. The GC-stress configuration is not part of this benchmark
workflow.

A timing is accepted only after the benchmark's result predicate succeeds.
Incorrect answers are recorded as `incorrect`, without a timing. Process
failures, timeouts, and unavailable reference implementations are recorded as
`crashed`, `timeout`, and `unsupported`.

## Comparing results

Compare runs made on the same host with the same input and runtime settings.
Do not compare a local Otium result directly with a historical number from the
upstream repository.

Every port has a parity grade:

- A is a mechanical syntax or name translation.
- B uses an equivalent representation, such as an Otium array in place of a
  Scheme vector.
- C contains a material semantic adaptation and is not a direct comparison.
- D is unsupported or replaces the measured algorithm.

Otium rows use `variant=port`. Unmodified Scheme rows use `variant=upstream`.
Keep that distinction in tables and charts even for A ports.

Use `reported_seconds` for the benchmark's timed region. `wall_seconds` includes
process startup, parsing, loading, and shutdown. Both are useful, but they answer
different questions. The input hash, upstream commit, Otium commit, platform,
flags, parity grade, and sample number are present to keep comparisons honest.

Canonical inputs were selected for mature Scheme implementations. They are
currently too large for useful Otium turnaround: the initial one-second probe
timed out on all 12 ports, and `fib:40:5` did not finish within 30 seconds. Use
the reduced-input tests for correctness while working on runtime performance.
Do not present those test runtimes as canonical benchmark results.

## Adding a port

Copy the algorithm into a new file under `ports/`, translate it to normal Otium,
and expose `benchmark-main`. Use `bench-read` for input and
`run-ported-benchmark` for timing and validation. Preserve setup outside the
timed thunk when the upstream program does.

Add the benchmark to `manifest.json` with a parity grade and a concrete
description of every deviation. Add a reduced input and expected result to
`test_ports.py`. Keep the submodule checkout and canonical input unchanged.

If a missing operation belongs in Otium generally, add it to the language with
tests and specification text. Do not add an R7RS compatibility shim merely to
make a port look closer to its source.
