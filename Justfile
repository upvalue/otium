# Otium development commands. Builds live in build/; see docs/development.md.

set positional-arguments

export PYTHONPATH := justfile_directory() / "tools" + ":" + justfile_directory()

# Configure and build the C runtime and native helpers
build:
    @test -d build/meson-info || meson setup build
    meson compile -C build

# Run tests; pass suite names (c, chez, shared, native, gpu, bench) or test names
test *args: build
    tools/meson-test default "$@"

# Default tests collecting at every C allocation. Very slow: only for work on the C collector
test-stress *args: build
    tools/meson-test stress "$@"

# Run every test, including native, GPU and benchmark ports
test-all: build
    meson test -C build --setup all --print-errorlogs

# Install the commit hook (repository-wide setting)
setup:
    git config core.hooksPath tools/hooks

# Format Otium source in place, or list changes with --check
fmt *args:
    @if [ "$#" -eq 0 ]; then set -- lib examples benchmarks manse; elif [ "$#" -eq 1 ] && [ "$1" = --check ]; then set -- --check lib examples benchmarks manse; fi; ./otium fmt "$@"

lint *args:
    @if [ "$#" -eq 0 ]; then set -- lib examples benchmarks manse; fi; OTIUM_PATH="{{justfile_directory()}}${OTIUM_PATH:+:$OTIUM_PATH}" ./otium lint "$@"

# Format C sources with clang-format, or check with --check
format-c *args:
    c/tools/format-c {{args}}

# Check documentation locations and the paths they name
check-docs:
    tools/check-docs

# Managed-pointer analysis of the C runtime; needs LLVM development tools
gc-lint *args:
    c/tools/gc-lint/run {{args}}

# Regenerate checked-in launcher catalogs and runtime reference tables
generate:
    python3 tools/generate-cli.py
    python3 tools/generate-reference.py
    python3 tools/generate-diagnostics.py

# Regenerate the local SDL ABI manifest from the installed headers
sdl-manifest *args:
    python3 native/sdl/setup.py {{args}}

# Wall-clock benchmarks; see benchmarks/README.md
bench *args: build
    python3 benchmarks/run.py {{args}}

# Native collector stress benchmarks
bench-gc: build
    meson compile -C build gc-bench
    build/gc-bench churn
    build/gc-bench mixed
    build/gc-bench fragmentation

clean:
    rm -rf build
