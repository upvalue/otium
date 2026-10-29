# Benchmarks

Run from the repository root after `just build`. Every runner uses the `./otium`
launcher; an optional positional executable (`--otium` for `timed.py`) selects
a custom build.

```sh
just bench
python3 benchmarks/timed.py
just bench-c
```

`run.py` runs every file in `workloads/`, or the paths given with `--only`.
`--runs`, `--warmups` and `--timeout` control sampling. These are fresh-process
wall times, including startup and compilation.

`timed.py` runs small checked workloads, including namespace calls, callbacks
and reloads, and reports both wall time and time inside the workload.

`just bench-c` runs the C runtime's warm microbenchmarks in `c/bench/runtime.c`,
calling each workload through the embedding API after warmup. Pass a workload
name to run one.

## Results

Runs write to ignored `benchmarks/runs/<family>/<timestamp>/` directories. Use
`--output FILE` for `run.py` and `timed.py` and `--output DIR` for R7RS.

Reports record commands, commit, dirty state, platform and a `--build-label`.
Cache state isn't normalized, so note it when comparing startup. Don't combine
results from different compilation policies.

## R7RS ports

`r7rs/ports/` holds Otium ports of selected programs from
[ecraven/r7rs-benchmarks](https://github.com/ecraven/r7rs-benchmarks), which is
pinned and unmodified as the `r7rs/vendor/` submodule. These are ports, not a
claim of R7RS compatibility. They keep the upstream algorithm, input, timing
boundary and result check where possible.

```sh
git submodule update --init benchmarks/r7rs/vendor
python3 benchmarks/r7rs/run.py --only fib ack
python3 benchmarks/r7rs/run.py --only fib --reference guile
just test bench
```

Omitting names runs every port. `--runs N` collects more samples and `--timeout`
sets the per-sample limit (default one sample, 300 seconds). With Guile
installed, `--reference guile` runs the unmodified upstream program on the same
host, feeding it the vendored input on standard input.

`just test bench` runs every port with reduced inputs and checks the answers.
The reduced `equal` input reports `INCORRECT`; that's a known,
unexplained failure, so the bench suite isn't green yet.

A timing counts only after the benchmark's own result check passes. Otherwise
the row is `incorrect`, `crashed`, `timeout` or `unsupported`. Use
`reported_seconds` for the timed region; `wall_seconds` includes startup,
loading and shutdown. Canonical inputs are sized for mature Schemes and are
currently too slow for useful turnaround, so use the reduced inputs while
working on performance and don't present those times as benchmark results.

`r7rs/manifest.json` grades each port and lists its deviations and any heap
limit:

- A: mechanical syntax or name translation.
- B: equivalent representation, such as an Otium array for a Scheme vector.
- C: a material semantic change; not a direct comparison.
- D: unsupported, or replaces the measured algorithm.

Otium rows use `variant=port` and upstream rows `variant=upstream`. Keep them
apart even for A ports, and only compare runs from the same host and settings.

Notes on specific ports:

- Otium's sequential `let` covers both `let` and `let*`. Named lets became local
  functions.
- `destruc` is why Otium has `set-car!` and `set-cdr!`; arrays would change what
  it measures.
- `quicksort` used an escape continuation only in its untimed checker. The port
  uses a boolean loop there.
- The canonical `mperm` input needs a heap well past the 64 MiB the runner gives other ports.
- `equal` uses structural equality on acyclic arrays in place of vectors.

To add a port, translate it into `r7rs/ports/`, expose `benchmark-main`, and use
`bench-read` and `run-ported-benchmark` from `r7rs/lib/benchmark.ot`. Keep setup
outside the timed thunk when upstream does. Add a manifest entry describing
every deviation and a reduced input in `r7rs/tests.py`. If a missing
operation belongs in Otium generally, add it to the language rather than a
compatibility shim.
