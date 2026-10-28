# Otium development commands. Builds live in build/<machine>-<os>/, one Meson
# configuration per subdirectory, so a checkout synced between machines keeps a
# build for each. See docs/language-guide.md.

set positional-arguments

export PYTHONPATH := justfile_directory() / "tools" + ":" + justfile_directory()

builds := justfile_directory() / "build" / `echo "$(uname -m)-$(uname -s | tr A-Z a-z)"`
export OTIUM_BUILD := env("OTIUM_BUILD", builds / "default")

# Configure and build the C runtime and native helpers
build:
    @test -d "$OTIUM_BUILD/meson-info" || meson setup "$OTIUM_BUILD"
    meson compile -C "$OTIUM_BUILD"

# Build the boot file native processes start from: the VM compiles the
# expander, core and the native compiler to native code
boot: build
    ./otium -L lib -e '(require otium.compiler.boot :as boot) (boot/build-standard "{{env("OTIUM_BUILD", builds / "default")}}/otium.boot")' > /dev/null

# Run tests; pass suite names (lang, c, native, gpu, bench) or test names
test *args: build
    tools/meson-test default "$@"

# Default tests collecting at every C allocation. Very slow: only for work on the C collector
test-stress *args: build
    tools/meson-test stress "$@"

# C runtime tests under ASan/UBSan with object kind checks, built in the asan configuration
sanitize *args:
    @test -d "{{builds}}/asan/meson-info" || meson setup "{{builds}}/asan" -Db_sanitize=address,undefined -Druntime_checks=kinds -Dui=disabled
    meson compile -C "{{builds}}/asan"
    ASAN_OPTIONS=detect_leaks=0 meson test -C "{{builds}}/asan" --suite c --print-errorlogs "$@"

# C runtime tests collecting at every allocation into fresh memory, so a missed root faults. Slow
check-roots *args:
    @test -d "{{builds}}/roots/meson-info" || meson setup "{{builds}}/roots" -Db_sanitize=address,undefined -Druntime_checks=roots -Dui=disabled
    meson compile -C "{{builds}}/roots"
    ASAN_OPTIONS=detect_leaks=0 meson test -C "{{builds}}/roots" --setup stress --suite c --print-errorlogs "$@"

# Run every test, including native, GPU and benchmark ports
test-all: build
    meson test -C "$OTIUM_BUILD" --setup all --print-errorlogs

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

# Count source lines with cloc; use --help for scopes and exclusions
loc *args:
    @python3 tools/loc "$@"

# Managed-pointer analysis of the C runtime; needs LLVM development tools
gc-lint *args:
    c/tools/gc-lint/run {{args}}

# Regenerate LLM-written docs from their .md.spec files; pass docs to pick some
docs *args:
    tools/generate-docs "$@"

# Regenerate checked-in launcher catalogs, runtime reference tables and docs/reference.json
generate:
    python3 tools/generate-cli.py
    python3 tools/generate-reference.py
    python3 tools/generate-diagnostics.py
    ./otium tools/export-reference.ot

# Regenerate the local SDL ABI manifest from the installed headers
sdl-manifest *args:
    python3 native/sdl/setup.py {{args}}

# Wall-clock benchmarks; see benchmarks/README.md
bench *args: build
    python3 benchmarks/run.py {{args}}

# C runtime microbenchmarks; pass a workload name to run one
bench-c *args: build
    meson compile -C "$OTIUM_BUILD" c/bench-runtime ffi-fixture-17.so
    "$OTIUM_BUILD/c/bench-runtime" {{args}}

# Remove this machine's builds
clean:
    rm -rf "{{builds}}"
