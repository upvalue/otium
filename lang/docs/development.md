# Repository development

Run commands from `lang/`. `make help` lists the supported entry points.

## Running Otium

```sh
make
./otium examples/greet.ot -- Alice
./otium --runtime=chez examples/greet.ot -- Alice
```

C is the default. `--runtime c` and `--runtime=c` are equivalent. The selector
can appear anywhere before `--`, except as the value of another option.
Everything after `--` is passed to the program unchanged. Repeated selectors
use the last value; each value must be valid. `CHEZ` selects the Chez executable.
`c/otium` and `chez/run` remain runtime-specific entry points.

Otium sources use `.ot`, including shared libraries, examples, and test fixtures.
Chez implementation programs use `.ss`, and Chez libraries use `.sls`. Vendored
Scheme keeps `.scm`. Module lookup still checks `.ot` then `.scm` in each search
directory; the compatibility and precedence tests intentionally create both.
`project.ot` contains module path configuration, not executable application code.

## Tests and dependencies

| Command | Coverage | Additional dependencies |
| --- | --- | --- |
| `make test` | C, Chez, portable library/reader/CLI/UI contracts, tooling, interactive REPL | C23 compiler, Python 3, Chez, POSIX PTYs |
| `make test-c`, `make test-chez` | Runtime-specific suites and shared conditions | Selected runtime |
| `make test-native` | FFI, SDL events/audio, UI layout; GPU checks when available | libffi, SDL3, CMake, C++20, glslc, pinned Yoga |
| `make test-gpu` | Required GPU and accelerated SDL acceptance | Native dependencies and working display/backend |
| `make test-benchmarks` | Reduced-input R7RS port correctness on both runtimes | Initialized benchmark submodule; optional Guile reference |
| `make test-site` | WebAssembly runtime and generated site checks | Emscripten, Node/npm, site dependencies |
| `make gc-lint` | C managed-pointer analysis | LLVM/Clang development tools |

`make ui-setup` downloads the pinned Yoga dependency; `make sdl-setup` generates
and validates the local SDL ABI manifest. `npm --prefix site ci` installs site
dependencies. Use `git submodule update --init benchmarks/r7rs/vendor` for the
upstream benchmark inputs. Default tests do not download optional dependencies.
A skipped graphics check is reported explicitly; `make test-gpu` requires it.

Use `tools/python` for repository Python programs. It honors `PYTHON` and sets
the module path without a global package install. Most shared suites accept
`--runtime c|chez|both`; parity-specific CLI checks always exercise both.

```sh
tools/python shared/tests/test_library.py --runtime chez
tools/python shared/tests/test_reader.py --runtime c --stress
```

Execution helpers live in `tools/otium_tools/`. They construct launcher commands,
resolve native helper defaults, and record benchmark provenance. Suite-specific
assertions, PTY control, GPU capture, and native probes stay beside their suites.
Helpers isolate project discovery by default; CLI and REPL discovery tests opt
out. Explicit `OTIUM_FFI_LIBRARY` overrides are preserved. Run tests with a clean
environment when checking the repository's default dependencies.

## Build outputs

C objects live under `c/build/<platform>/obj/<collector>-ffi<mode>-<flags-hash>/`.
Different checkouts and compiler configurations do not share objects. `BUILD_ROOT`,
`BIN_ROOT`, `PLATFORM`, `GC`, and `WITH_FFI` remain configurable. The C launcher
selects `c/bin/<platform>/otium`; `OTIUM_BIN_ROOT` and `OTIUM_PLATFORM` override
its location. Shared native outputs live in `shared/build/<platform>/`.

A root Make invocation uses one FFI configuration. Native targets select FFI
for the whole invocation, including `make -j test test-native`. Do not run two
independent builds that select different configurations into the same public
binary path; use separate worktrees or `BIN_ROOT` values for that case.

## Generated files

| Source | Outputs | Regeneration/check |
| --- | --- | --- |
| `shared/cli/options.json` and generator | C/Chez launcher catalogs | `make generate` / `make check-generated` |
| `shared/documentation/runtime.json` | C/Chez runtime reference tables | Same aggregate targets |
| Shared `.ot` libraries | Embedded C headers | Native/WASM Make dependencies; build-only |
| Language docs and source docstrings | Site reference and pages | `make site`; ignored outputs |
| UI shader sources | Embedded shader header and SPIR-V | CMake dependencies; build-only |
| Installed SDL headers and schema | Target ABI manifest | `make sdl-setup`; validate with `tools/python shared/sdl/setup.py --check` |

## Ownership

`docs/` explains current language behavior and development workflows. `specs/`
records design contracts and proposals; check implementation and executable tests
when a proposal differs from current behavior. `GAPS.txt` tracks portability
limits. Runtime implementation notes belong under `c/` or `chez/`; benchmark
methodology belongs under `benchmarks/`. Shared native dependencies and portable
Otium libraries belong under `shared/`.

See [the example catalog](examples.md) for runnable programs and
[benchmark usage](../benchmarks/USAGE.md) for measurements and saved results.
