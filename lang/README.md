# otium language

experimental little programming language

## Build and bootstrap

The supported runtime always embeds `prelude/expander.scm` and
`prelude/prelude.scm`. Meson generates C headers for both files and the VM loads
them before it creates the `user` namespace. A prelude-free runtime is not a
supported build mode.

```sh
meson setup build
meson compile -C build
meson test -C build
```

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
