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
```

`make lib` builds the embeddable runtime as `build/libotium.a`; `make bench`
runs the benchmark suite. Defaults and feature switches live in `config.mk`.
Put machine-local overrides such as sanitizer flags or Raylib paths in an
untracked `site.mk`.

## Project files

A checkout can record the load-path directories it needs in a `project.ot` at
its root. `otium` searches the working directory and its ancestors for the
nearest one and appends its directories to the module search path, so a source
file runs the same way from the shell, from CI, and from an editor client that
only controls the working directory:

```lisp
(paths "ext/ray" "ext/demo")
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

## Native extensions

Optional native modules are statically linked into the `otium` executable and
left out of `libotium`. The demo module is always available. The Raylib module
is included when `pkg-config` finds Raylib; set `WITH_RAY=0` in `site.mk` to
disable it.

The repository includes a dependency-free demo and a Raylib binding. See
[ext/README.md](ext/README.md) for build commands, the extension API, and the
Raylib example.
