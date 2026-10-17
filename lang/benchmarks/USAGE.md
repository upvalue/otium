# Benchmarks

Workloads are organized by what they measure. Select a runtime with `--runtime c|chez`. An optional positional executable
keeps support for custom runtime builds.

From the repository root:

```sh
make
tools/python benchmarks/run.py --runtime c
tools/python benchmarks/run.py --runtime chez
tools/python benchmarks/compare.py --otium c/otium
```

`run.py` runs every file in `workloads/` by default. Supply paths after the
executable to select workloads. `--runs`, `--warmups`, and `--timeout` control
sampling. These are fresh-process wall times, including startup and compilation.
The sources and result checks are identical on both runtimes.

`compare.py` checks paired workloads and reports both wall time and time inside
the workload. Both runtimes execute the same source, including forwarding callbacks and
module reloads.

The R7RS ports also share source. See [R7RS usage](r7rs/USAGE.md) for canonical
inputs, runtime flags, upstream references, and reduced-input checks.

## Runtime diagnostics

`gc/compare.py` builds and compares C collectors under a common memory budget.
It uses `c/benchmarks/gc_bench.c` and the shared allocation workloads:

```sh
tools/python benchmarks/gc/compare.py --runs 1 --warmups 0
```

`c/README.md` is retained as historical documentation. Current commands and
paths are listed here.

## Results and provenance

Runs write to ignored `benchmarks/runs/<family>/<timestamp>/` directories by
default. Use `--output FILE` for the workload and paired runners, or `--output DIR`
for R7RS. Curated historical results live under `benchmarks/results/`; promote
new results there deliberately. GC comparison retains its explicit `--csv` output.

Reports record commands, commit, dirty state, platform, and a `--build-label`.
The paired runner deliberately uses direct Chez without imported-library
compilation, preserving its existing measurement boundary. Other runners use the
normal launcher. Cache state is not normalized; record it when comparing startup
performance. Do not combine results from different compilation policies.
