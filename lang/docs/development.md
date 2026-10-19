# Repository development

Run commands from `lang/`. `just --list` shows them all.

```sh
just build
./otium examples/greet.ot -- Alice
./otium --runtime=c examples/greet.ot -- Alice
just test
```

Chez is the default runtime. `--runtime c` and `--runtime=c` are equivalent; the
selector can appear anywhere before `--`, except as another option's value.
Everything after `--` goes to the program unchanged. `CHEZ` selects the Chez
executable. Both runtimes search `lib/` after any user paths, so the standard
library needs no `--path`.

Otium sources use `.ot`. Chez implementation programs use `.ss` and Chez
libraries `.sls`; vendored Scheme keeps `.scm`. Module lookup checks `.ot` then
`.scm` in each search directory. `project.ot` holds load paths, not code.

## Layout

| Directory | Contents |
| --- | --- |
| `c/` | C runtime (`src/`), its tests, the GC benchmark and gc-lint |
| `chez/` | Chez runtime, its launcher `chez/run`, and its tests |
| `lib/` | Otium source for both runtimes: prelude, expander, `otium.*` libraries and tools |
| `lib/catalog/` | Launcher options and runtime reference data, compiled into both runtimes |
| `native/` | C and C++ bridges both runtimes load; see [native components](native.md) |
| `tests/` | Cross-runtime tests and the Python drivers that need process control |
| `examples/`, `benchmarks/`, `docs/`, `specs/` | What they say |
| `tools/` | Development scripts, the dev-server client, the commit hook |

## Building

`just build` runs `meson setup build` once, then `meson compile -C build`. The C
binary is `build/otium`. FFI and UI are built when their dependencies are
installed; see [native components](native.md). Collector and heap settings are
Meson options:

```sh
meson configure build -Dgc=gen
meson setup build-small -Dheap_max=8388608 -Dffi=disabled
OTIUM_BUILD=$PWD/build-small ./otium --runtime=c examples/greet.ot -- Alice
```

`OTIUM_BUILD` tells `./otium` and `chez/run` which build directory to use; the
default is `build/`.

## Tests

`just test` runs the default tests. Name suites or tests to narrow it.

| Suite | Coverage | Default |
| --- | --- | --- |
| `c` | Runtime unit tests, conformance files, CLI, multimethods, extension | yes |
| `chez` | Chez runtime tests | yes |
| `shared` | Library, reader, conditions, modules, CLI and UI checks on both runtimes; formatter; dev server; docs | yes |
| `native` | FFI, SDL events and audio, UI layout and GPU capture (skips without a display) | no |
| `gpu` | Required accelerated SDL and GPU checks, live UI repair | no |
| `bench` | Reduced-input R7RS port checks | no |

```sh
just test chez
just test match
just test native
just test-stress            # default tests with OTIUM_GC_STRESS=1
just test-all
meson test -C build --list
```

GC stress collects at every C allocation and is slow. Use it when changing the
collector or code that holds managed pointers, along with `just gc-lint`, which
needs LLVM development tools.

Most cross-runtime checks are `.ot` files under `tests/` that exit nonzero on
failure; `meson.build` runs each on both runtimes. Python drivers remain where a
test needs temporary module trees, sockets, PTYs, signals or frame timing.

## Formatting and linting

```sh
./otium fmt lib/otium/ui.ot   # rewrite in place
./otium fmt --check lib examples
./otium lint lib examples
just format-c --check
```

Both Otium tools take files or directories, run on Chez, and are experimental.
The linter only warns; its one rule, `if-chain`, flags an `if` in the else
branch of another `if`.

The formatter uses a width of 120. A form stays on one line if it fits, except
that these always break: function definitions, bodies of two or more forms, an
`if` with an else branch, and a `let` with two or more bindings, which go one
per line. Other line breaks in the input are ignored. Comments and single blank
lines are kept, and top-level definitions get a blank line between them. Forms
without a rule are laid out as calls, with arguments aligned under the first.
A project can lay out its own macros like a built-in form:

```scheme
(paths "examples")
(format (like when with-slots) (like let match-let))
```

Rules can use qualified names, such as `(format (like unwind-protect my.library/body))`.
The formatter resolves explicit `:as` aliases in top-level `require` forms and
`ns` require clauses, in source order and per namespace, and follows top-level
`begin` forms. It doesn't load libraries, expand macros or resolve `:refer`. A
rule for the literal name in the source wins over an alias rule.
`otium.cli/compile` always starts its arguments on the next line, indented two
spaces. Don't put strings in the `format` entry: launchers read every string in
`project.ot` as a load path.

Both tools read source with the Chez reader in token mode
(`otium.reader/tokens`), which records each token's text and position. The tree,
formatter and linter are in `lib/otium/`.

## Commit hook

`just setup` sets `core.hooksPath` for the repository. The hook checks staged
`lang/` files only: doc locations and named paths (`tools/check-docs`), Otium
formatting of added or edited files under `lib/`, `examples/` and `benchmarks/`,
and `just format-c --check` when C under `c/`, `native/` or `tests/` changes.
The gc-lint fixtures in `c/tools/gc-lint/tests/` are left unformatted.

Docs belong in `docs/` (current behavior) or `specs/` (designs and proposals),
plus the READMEs `tools/check-docs` allows. When a doc goes out of date, delete
the stale part rather than recording what used to be true.

## Generated files

| Source | Outputs | Command |
| --- | --- | --- |
| `lib/catalog/options.json` | `c/src/options.inc`, `chez/otium/launcher-options.sls` | `just generate` |
| `lib/catalog/runtime.json` | `c/src/runtime-reference.inc`, `chez/otium/runtime-documentation.sls` | `just generate` |
| Installed SDL headers | `native/sdl/manifests/<target>.json` | `just sdl-manifest` |

The generated files are checked in. The `generated` and `help` tests fail when
they're stale. The C runtime embeds its Otium libraries from `lib/otium/` with
`#embed` at build time.

## Where things go

`docs/` explains current behavior and workflows; `specs/` records design
contracts and proposals, so check the code and tests when a proposal differs.
Portability limits are in [portability](portability.md). C and C++ in `native/`
are only for bridges that would be impractical in Otium; library orchestration
and application policy belong in Otium.

See the [example catalog](examples.md) and [benchmarks](../benchmarks/README.md).
