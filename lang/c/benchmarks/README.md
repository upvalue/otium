# Otium benchmarks

Each benchmark is a standalone Otium program. A case should perform its work,
check its result, and produce no output on success. `run.py` launches each case
in a fresh process, performs warmup runs, and reports the median, minimum, and
maximum wall-clock time.

Because each sample is a fresh process, measurements include VM creation and
prelude loading. This makes the harness useful before Otium has an in-process
clock API and keeps benchmark programs independent. The runner can later be
replaced without changing the programs.

To add a benchmark:

1. Add a `.scm` program in this directory with a correctness check.
2. Run `benchmarks/run.py path/to/otium`. The runner discovers every `.scm`
   file in this directory.

The runner also accepts one or more benchmark paths directly:

```sh
benchmarks/run.py path/to/otium --runs 10 benchmarks/fib.scm
```

## GC comparison

`gc_bench.c` is the in-process collector harness. It has short-lived list,
mixed-lifetime, and fragmentation workloads. It can also evaluate a standalone
Otium benchmark after bootstrapping the runtime. Both paths reset the GC
counters before the measured work and report the same JSON record.

Build and run the direct workloads for one collector with:

```sh
make bench-gc GC=gen
```

`gc_compare.py` builds semispace and `gen` harnesses in separate build
directories. It chooses build geometry that puts their initial
`reserved_bytes + metadata_bytes` under the same target, runs each sample in a
fresh process, and prints a Markdown table. By default it runs the three direct
workloads plus `gcbench.scm`, `destruc.scm`, `mperm.scm`, and `takl.scm`:

```sh
python3 benchmarks/gc_compare.py --budget-mib 128 --runs 5
```

Use `--workload gcbench` to select one workload. Use
`--csv benchmarks/gc-results.csv` to retain every raw sample. The reported
collection tuple is full-copy/minor/major-sweep/major-compact.

Use `--collector gen` to run only the generational collector. Add `--append-csv`
to accumulate samples over time. Each CSV row records its UTC
timestamp, Otium revision and dirty-worktree flag, host, OS, architecture,
memory budget, build heap geometry, warmup count, and measured-run count.
Language workload rows also record a hash of the source file. Timeouts,
incorrect results, process failures, and malformed output stay in the raw CSV
with a status and reason. They are not included in timing summaries.

GC percentage and maximum pause use the nesting-aware mutator-pause counter.
The phase tuple remains useful for identifying where each stop spent its time.

### Initial arm64 result

The first result was recorded on 2026-08-21 on an Apple arm64 laptop running
macOS 15.6.1 and Clang 17. Both collectors had a 128 MiB
reservation-plus-metadata budget. Each row is the median of five fresh
processes after one warmup; maximum pause is the largest observed pause across
those five samples.

| collector | workload | wall ms | workload ms | MiB/s | GC % | max pause ms | collections F/M/S/C |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| semi | churn | 12.29 | 8.03 | 4749.4 | 21.6 | 0.051 | 49/0/0/0 |
| gen | churn | 12.49 | 8.04 | 4745.2 | 5.7 | 0.083 | 0/20/0/0 |
| semi | mixed | 15.04 | 10.88 | 803.3 | 84.2 | 0.277 | 70/0/0/0 |
| gen | mixed | 8.25 | 3.62 | 2413.7 | 43.1 | 0.537 | 0/6/1/0 |
| semi | fragmentation | 4.72 | 0.71 | 1200.3 | 63.7 | 0.195 | 3/0/0/0 |
| gen | fragmentation | 6.18 | 1.64 | 519.8 | 82.0 | 0.675 | 0/2/0/3 |

The generational collector matches churn throughput and cuts the mixed-case
workload time substantially. Its longest pauses are higher in all three cases,
and forced compaction loses on both throughput and pause time. These synthetic
results supported keeping `semi` as the default at the time of that run.

The four language GC workloads are adapted from the ports under
`benchmarks/r7rs/` and the pinned ecraven/r7rs-benchmarks source at commit
`85f6acdc4cc4e2b857f307ba56bd0ba931dcccd1`. They use shorter Otium-oriented
inputs so a normal five-sample comparison finishes locally: gcbench depth 15,
30 destruc repetitions at 600/50, mperm at 8/8/2/1, and seven takl repetitions
at 18/12/6. These are collector workloads, not canonical R7RS timing results.

## Tree evaluator to bytecode VM

The VM migration baseline was recorded on 2026-08-15 on an Apple arm64 machine
running macOS 15.6.1 and Clang 17. Both binaries were Meson release builds with
computed-goto dispatch. Each result is the median of three fresh processes
after one warmup.

| benchmark | tree evaluator | bytecode VM | speedup |
| --- | ---: | ---: | ---: |
| fib | 909.30 ms | 113.19 ms | 8.03x |
| loop | 703.18 ms | 99.82 ms | 7.04x |
| tables | 71.31 ms | 16.82 ms | 4.24x |
