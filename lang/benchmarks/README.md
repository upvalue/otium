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
