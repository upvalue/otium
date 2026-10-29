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

The R7RS and PLB runners also record each sample's peak RSS in `max_rss_kb`.
Linux carries a process's RSS high-water mark across `exec`, so a program
started straight from the Python runner would report at least the runner's own
peak. `common.py` runs each sample through `maxrss.c`, a small helper it
compiles into the build directory on first use, so the floor is about 1 MB.
Without a C compiler the column is empty.

Both runners take `--otium-modes native vm` to run Otium as native code, on the
VM (`--c.vm`), or both; each mode gets its own implementation name, such as
`otium-native-3a19b55`. The default is native. `--cpu auto` pins every process
to one CPU with `taskset`.

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
sets the per-sample limit (default one sample, 300 seconds). Each port gets a
1 GiB maximum heap unless its manifest entry sets `heap_max`; the heap grows
with occupancy, so the ceiling costs small ports nothing. With Guile
installed, `--reference guile` runs the unmodified upstream program on the same
host, feeding it the vendored input on standard input.

`just test bench` runs every port with reduced inputs and checks the answers.

A timing counts only after the benchmark's own result check passes. Otherwise
the row is `incorrect`, `crashed`, `timeout` or `unsupported`. Use
`reported_seconds` for the timed region; `wall_seconds` includes startup,
loading and shutdown. Present only canonical-input times as benchmark
results. A full canonical run of native code took about three minutes on an
x86-64 Linux desktop in October 2026. `earley` still exhausts its 1 GiB heap
there (ticket oti-g4kr), and `paraffins` and `nboyer` peak at about 2.1 GB of
RSS (oti-p9sf).

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
- The canonical `mperm` input needs a heap past the 1 GiB the runner gives other ports.
- `equal` uses structural equality on acyclic arrays in place of vectors.

To add a port, translate it into `r7rs/ports/`, expose `benchmark-main`, and use
`bench-read` and `run-ported-benchmark` from `r7rs/lib/benchmark.ot`. Keep setup
outside the timed thunk when upstream does. Add a manifest entry describing
every deviation and a reduced input in `r7rs/tests.py`. If a missing
operation belongs in Otium generally, add it to the language rather than a
compatibility shim.

## Programming-Language-Benchmarks

`plb/` runs problems from
[hanabi1224/Programming-Language-Benchmarks](https://github.com/hanabi1224/Programming-Language-Benchmarks)
on Otium and on the reference languages installed on the host: Python, SBCL,
Lua and LuaJIT. These cover floats, hash tables, bytevectors, output and
allocation, which the R7RS suite barely touches.

```sh
python3 benchmarks/plb/run.py --only nbody --size small
python3 benchmarks/plb/run.py --languages otium python --otium-modes native vm
just test bench
```

Upstream is about 160 MB, almost all of it knucleotide's input archive, which
is only `fasta` output. `plb/vendor/` holds an unmodified copy of just the files
`plb/manifest.json` uses: reference programs, expected outputs and licenses.
`python3 benchmarks/plb/vendor.py [COMMIT]` refreshes it and records the
commit. The runner generates knucleotide's input with upstream's Python fasta.

`plb/manifest.json` lists each problem's checked inputs, its `small` and `full`
timed inputs, the reference program for each language and the Otium port's
grade and deviations, on the same A to D scale as the R7RS ports. References
are upstream's official entries for each language, single-threaded where
upstream offers a choice. Gaps stay gaps: where upstream has no program for a
language, the cell is empty rather than filled with one we wrote. Problems
needing bignums, regexes or networking aren't included.

`--size test` checks every implementation against upstream's expected output.
Timed sizes have no expected output upstream, so the first reference that runs
sets the answer, by SHA-256 of its output, and later implementations must
match it. `full` is the default; `small` is quicker. Times are whole-process
wall time, as upstream measures them, so they include startup and SBCL's
image load. SBCL programs are built into an image first, as upstream does,
and the build isn't timed.

Every process runs on one CPU (`--cpu auto` is the default here). Some
reference programs were written for several cores; the manifest marks them
`threaded`. Pinned Python runs set `PYTHON_CPU_COUNT=1` so multiprocessing
programs use one process. That also avoids a crash in upstream's knucleotide
under Python 3.14's default `forkserver` start method.

Ports read their arguments after `--` and share `plb/lib/plb/support.ot` for
printf-style number formatting. `project.ot` puts `plb/lib` on the load path
for `just lint`. To add a port, write `plb/ports/NAME.ot` after an upstream
program, add the problem's references, sizes and grade to the manifest, run
`vendor.py` to fetch its files, and check it with `--size test`.

## Extensive report

`benchmarks/extensive-report.py` runs both suites against every installed
reference, with Otium both native and on the VM, three samples each at full
sizes, and writes a self-contained HTML report under `benchmarks/runs/report/`.
It takes a long while, so it's meant for once or twice a week, not for checking
whether a change helped; use the runners directly for that.

```sh
python3 benchmarks/extensive-report.py
python3 benchmarks/extensive-report.py --r7rs RUN_DIR... --plb RUN_DIR...
```

It prints which implementations are installed first and reports missing ones
as "not installed". Given existing run directories it reports on them instead
of running; several directories for one suite, such as runs from two commits,
become separate columns. Cells show each implementation's median time and its
ratio to native Otium (`--baseline` picks another column), shaded by how many
times faster or slower it is. A peak memory table follows each time table.
