# r7rs benchmark report

Point-in-time snapshot: 2026-08-15

This report records the state reached while working on LAN-70GB. It is based on
the work and measurements discussed during that work. No additional benchmark
runs were made for this report.

## Current state

The initial r7rs benchmark layer is on `main` at Otium commit `522623b`. It ports
12 programs from ecraven/r7rs-benchmarks:

- calls and recursion: `fib`, `ack`, `tak`, `cpstak`
- lists: `takl`, `nqueens`, `deriv`
- allocation and mutation: `destruc`, `mperm`, `gcbench`
- arrays and floating point: `quicksort`, `fft`

The upstream repository is a submodule at `benchmarks/r7rs/vendor`, pinned to
commit `85f6acdc4cc4e2b857f307ba56bd0ba931dcccd1`. The Otium layer stays in the
parent repository:

- `benchmarks/r7rs/ports/` contains native Otium ports.
- `benchmarks/r7rs/lib/benchmark.scm` contains the shared benchmark driver.
- `benchmarks/r7rs/manifest.json` records parity grades and deviations.
- `benchmarks/r7rs/run.py` runs Otium and an optional same-host Guile reference.
- `benchmarks/r7rs/results.csv` and `STATUS.md` record raw and readable results.
- `benchmarks/r7rs/test_ports.py` checks all ports with reduced inputs.

These are Otium ports, not an R7RS compatibility layer. The comparison keeps the
upstream algorithm, inputs, timing boundary, and result check where possible.
Otium results are labeled `variant=port`; unmodified Scheme results are labeled
`variant=upstream`. Each port has an A or B parity grade.

## Otium changes made for the ports

The work added generally useful numeric procedures: `sqrt`, `exp`, `log`, the
standard trigonometric functions, one- and two-argument `atan`, `expt`, and
`truncate`. It also added exactness conversions and numeric classification
predicates.

Benchmark timing uses `current-jiffy` backed by a monotonic nanosecond clock,
`jiffies-per-second` returning 1,000,000,000, and `current-second` backed by the
system clock.

The CLI now accepts `--max-depth`, `--stack-slots`, `--heap-init`, and
`--heap-max`. Existing defaults remain in place unless overridden. The runner
records its runtime flags with each sample.

`destruc` required `set-car!` and `set-cdr!`. Pair mutation rejects cycles, and
pairs used as structural table keys are frozen so their hashes cannot change.

## Correctness and verification

All 12 ports pass their reduced-input correctness checks. The optional Guile
assembly path also passed when Guile was available.

After rebasing onto the then-current `main`, the normal Meson suite passed 3/3
in both the worktree and main checkout:

```text
otium-cli    OK
r7rs-ports   OK
otium-tests  OK
```

GC-stress testing was explicitly excluded from this work and was not used for
verification.

## Performance snapshot

The canonical inputs are currently too large for useful Otium turnaround. All
12 ports exceeded a one-second probe. Canonical `fib:40:5` also failed to finish
within 30 seconds.

Six matched reduced workloads were run once in Otium and once through the
unmodified upstream Guile path. The table uses each implementation's reported
time for the benchmark's timed region, excluding process startup.

| benchmark | Otium | Guile | Otium / Guile |
|---|---:|---:|---:|
| `fib` | 7.228s | 0.153s | 47x |
| `tak` | 0.229s | 0.005s | 44x |
| `deriv` | 1.199s | 0.028s | 42x |
| `nqueens` | 0.195s | 0.005s | 40x |
| `fft` | 0.095s | 0.004s | 24x |
| `quicksort` | 0.094s | 0.005s | 18x |

These are single samples on reduced inputs. Guile auto-compilation was disabled.
The short Guile timings are especially sensitive to measurement noise, so these
ratios are directional rather than publication-quality results.

The pattern is still clear. Recursive evaluator-heavy programs are roughly 40x
to 47x slower than Guile in this sample. The array-heavy ports are less bad at
roughly 18x to 24x slower. The benchmark ports are validating the intended
algorithms, so the current gap points at Otium evaluation and runtime costs
rather than time spent in a compatibility layer.

The checked-in `results.csv` is the earlier one-second canonical probe. Its
implementation label refers to the pre-merge dirty worktree, not final commit
`522623b`, and every row is a timeout. It is useful as a record of that probe but
should not be treated as the baseline for the final commit.

## What to do next

The next useful measurement step is a checked-in reduced or scaled input profile
that both Otium and upstream implementations can run for long enough to measure
reliably. That would give us repeatable trend lines while canonical inputs remain
out of reach. It should use multiple samples and preserve the same input for both
implementations.

After that, the recursive ports give a direct target for evaluator and call-path
work. `fft` and `quicksort` provide a separate view of arrays, numeric operations,
and mutation. Canonical runs can become a milestone once reduced-input trends
show enough improvement to make them practical.

The benchmark setup itself is ready for that work. The current performance is
not close to Guile.
