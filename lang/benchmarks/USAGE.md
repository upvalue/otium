# Benchmarks

Workloads are organized by what they measure. Pass either `c/otium` or
`chez/run` to the shared runners.

From the repository root:

```sh
make
python3 benchmarks/run.py c/otium
python3 benchmarks/run.py chez/run
python3 benchmarks/compare.py --otium c/otium
```

`run.py` runs every file in `workloads/` by default. Supply paths after the
executable to select workloads. `--runs`, `--warmups`, and `--timeout` control
sampling. These are fresh-process wall times, including startup and compilation.
The sources and result checks are identical on both runtimes.

`compare.py` checks paired workloads and reports both wall time and time inside
the workload. Tables, clocks, and imports use shared APIs. Live callbacks and
reload retain runtime adapters because their loader behavior differs.

The R7RS ports also share source. See [R7RS usage](r7rs/USAGE.md) for canonical
inputs, runtime flags, upstream references, and reduced-input checks.

## Runtime diagnostics

`gc/compare.py` builds and compares C collectors under a common memory budget.
It uses `c/benchmarks/gc_bench.c` and the shared allocation workloads:

```sh
python3 benchmarks/gc/compare.py --runs 1 --warmups 0
```

`c/README.md` is retained as historical documentation. Current commands and
paths are listed here.
