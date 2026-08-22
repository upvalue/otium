# otium language

experimental little programming language

## Build and bootstrap

The supported runtime always embeds `prelude/expander.scm` and
`prelude/prelude.scm`. Make generates C headers for both files and the runtime
loads them before it creates the `user` namespace. A prelude-free runtime is
not a supported build mode.

```sh
make
make test
./otium
```

Build objects, generated headers, the embeddable library, and test programs go
under `/tmp/otium/<architecture>-<os>/`. The CLI is the exception: it goes in
`bin/<architecture>-<os>/otium`, and the `./otium` symlink runs the binary for
the current platform. This keeps the large intermediate files out of synced
checkouts without making the usual command platform-specific.

`make lib` builds the embeddable runtime as
`/tmp/otium/<architecture>-<os>/libotium.a`; `make bench` runs the benchmark
suite. Defaults and feature switches live in `config.mk`. Put machine-local
overrides such as sanitizer flags, Raylib paths, `BUILD_ROOT`, or `BIN_ROOT` in
an untracked `site.mk`. If `BIN_ROOT` changes, set the launcher's corresponding
`OTIUM_BIN_ROOT` environment variable too. `OTIUM_PLATFORM` can select a
non-native CLI build at runtime.

## Garbage collectors

The default `GC=semi` build uses the original whole-heap copying collector.
`GC=gen` selects the copying-nursery and mark-sweep old-space collector.
`GC=gsgc` selects the generation-scavenging collector transplanted from GSGC
1.0:

```sh
make GC=gen
make test GC=gen
make test GC=gsgc
```

The `gen` collector's architecture is adapted from the BSD-licensed Dartino
collector maintained in the [Toit repository](https://github.com/toitlang/toit/tree/d0396578ff5b7cf9d1ea1509421ec82fa6afeef1/src/third_party/dartino).
The source checkout is pinned at `d0396578ff5b7cf9d1ea1509421ec82fa6afeef1`.
It was checked on 2026-08-21; the last change to that subtree was
`b21477806e6b8ba9e18c570e803fbe529f258054` on 2024-05-01. See [Credits](#credits)
for provenance and [`agent-docs/gc-gen.md`](agent-docs/gc-gen.md) for Otium's
layout and departures from upstream.

Otium calls the implementation `gen`. Arrays and byte objects remain
contiguous, including objects that span old-space cards. Host tuning defaults
for the nursery, old-space growth chunks, large-object cutoff, mark stack, and
pause timing live in `config.mk`.

The `gsgc` build keeps GSGC's two copying generations, age-based promotion,
and remembered-object set. It uses Otium's exact root and object walker instead
of GSGC's public API and pointer maps. See [Credits](#credits) for provenance
and [`agent-docs/gc-gsgc.md`](agent-docs/gc-gsgc.md) for the implementation and
memory geometry.

## Project files

A checkout can record the load-path directories it needs in a `project.ot` at
its root. `otium` searches the working directory and its ancestors for the
nearest one and appends its directories to the module search path, so a source
file runs the same way from the shell, from CI, and from an editor client that
only controls the working directory:

```lisp
(paths "examples/ray" "examples/demo")
```

Relative entries resolve against the directory holding the file, not the working
directory. `--path` and `OTIUM_PATH` keep their higher priority, and
`--no-project` ignores the file entirely.

The file holds directive forms rather than a data literal, and it is read but
never evaluated: `{...}` and `[...]` read as constructor calls, and starting a
program by running arbitrary code is a trapdoor a path list does not need.
`paths` is the only directive so far; anything else is reported and skipped.

## Formatting

Run `tools/format-c` to format the C sources. Run
`tools/format-c --check` to check them without making changes.

The script looks for `clang-format` on `PATH` and through Xcode's `xcrun`.
Set `CLANG_FORMAT` to use a specific binary.

## Editor support

Vim and Neovim runtime files are available in [`vim/`](vim/README.md).

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

For a collector comparison with the same reservation-plus-metadata budget:

```sh
python3 benchmarks/gc_compare.py --budget-mib 128 --runs 5
```

## Native extensions

Optional native modules are statically linked into the `otium` executable and
left out of `libotium`. The demo module is always available. The Raylib module
is included when `pkg-config` finds Raylib; set `WITH_RAY=0` in `site.mk` to
disable it.

The repository includes a dependency-free demo and a Raylib binding. See
[examples/README.md](examples/README.md) for build commands, the extension API, and the
Raylib example.

## Credits

Otium currently adapts two different GC algorithms for use, both under BSD license.

- [toit](https://github.com/toitlang/toit)'s garbage collector, originally from Dartino.
- gsgc by Ian Piumarta.
