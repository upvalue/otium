# otium language

experimental little programming language

## Formatting

Run `tools/format-cpp` to format the C++ sources. Run
`tools/format-cpp --check` to check them without making changes.

The script looks for `clang-format` on `PATH` and through Xcode's `xcrun`.
Set `CLANG_FORMAT` to use a specific binary.

## Benchmarks

Pass an optimized `otium` binary to the benchmark runner:

```sh
benchmarks/run.py path/to/otium
```

The benchmark runner performs one warmup followed by five measured runs. To
change the sample count or run individual benchmark programs directly:

```sh
benchmarks/run.py path/to/otium --warmups 2 --runs 10 benchmarks/fib.scm
```

See [benchmarks/README.md](benchmarks/README.md) for how to add cases and what
the measurements include.
