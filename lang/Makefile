.DEFAULT_GOAL := all
PYTHON ?= python3
export PYTHON
RUN_PYTHON := tools/python
# One invocation uses one C configuration, including optional native targets.
ifneq ($(filter ffi ui test-ffi test-sdl test-sdl-integration test-ui-layout test-ui-gpu test-native test-gpu,$(MAKECMDGOALS)),)
WITH_FFI := 1
endif
WITH_FFI ?= 0
export WITH_FFI


.PHONY: clean all lib test test-c test-chez test-expander test-sdl test-sdl-integration test-repl sdl-setup bench bench-gc format hygiene gc-lint

# Keep the existing C entry points; implementation rules live with the runtime.
all lib bench bench-gc format hygiene gc-lint:
	$(MAKE) -C c $@

test: test-tools check-generated test-c test-chez test-expander test-classes test-match test-ui test-cli test-reader test-library test-format
	$(MAKE) test-repl

test-c: all
	$(MAKE) -C c test
	$(RUN_PYTHON) shared/tests/test_conditions.py --runtime c

test-chez:
	chez/test
	$(RUN_PYTHON) shared/tests/test_conditions.py --runtime chez

# One set of expander checks, run unchanged on both runtimes.
test-expander: all
	./otium --runtime=c --no-project shared/tests/expander/checks.ot
	OTIUM_GC_STRESS=1 ./otium --runtime=c --no-project shared/tests/expander/checks.ot
	./otium --runtime=chez --no-project shared/tests/expander/checks.ot

sdl-setup:
	$(RUN_PYTHON) shared/sdl/setup.py

test-sdl: ui
	$(RUN_PYTHON) shared/tests/test_sdl.py --runtime both
	$(RUN_PYTHON) shared/tests/test_ui_gpu.py

test-sdl-integration: ffi
	$(RUN_PYTHON) shared/tests/test_sdl.py --runtime both --require-accelerated

test-repl: all
	$(MAKE) -C shared
	$(RUN_PYTHON) shared/tests/test_help.py
	$(RUN_PYTHON) shared/tests/test_repl.py

clean:
	$(MAKE) -C c clean
	$(MAKE) -C shared clean

.PHONY: test-ffi ffi
ffi: all
	$(MAKE) -C shared ffi

test-ffi: ffi
	$(RUN_PYTHON) shared/tests/test_ffi.py --stress

.PHONY: test-cli
test-cli: all
	$(RUN_PYTHON) shared/cli/generate.py --check
	$(RUN_PYTHON) shared/tests/test_cli.py

.PHONY: test-reader
test-reader: all
	$(RUN_PYTHON) shared/tests/test_reader.py
	$(RUN_PYTHON) shared/tests/test_reader.py --stress

# The Otium formatter and linter run on Chez. The corpus check formats the
# tree in memory and leaves files untouched.
.PHONY: test-format
test-format:
	./otium --runtime=chez --no-project --path shared/lib shared/tests/format/checks.ot
	./otium --runtime=chez --no-project --path shared/lib shared/tests/format/corpus.ot -- shared examples benchmarks

# ✨ Markdown documentation and the browser C runtime.
.PHONY: site test-site
site:
	npm --prefix site run build

test-site: site
	node site/tests/runtime.mjs
	$(RUN_PYTHON) site/tests/check_site.py

.PHONY: test-classes
test-classes: all
	./otium --runtime=c --no-project shared/tests/classes/checks.ot
	OTIUM_GC_STRESS=1 ./otium --runtime=c --no-project shared/tests/classes/checks.ot
	./otium --runtime=chez --no-project shared/tests/classes/checks.ot

.PHONY: test-match
test-match: all
	./otium --runtime=c --no-project shared/tests/match/checks.ot
	OTIUM_GC_STRESS=1 ./otium --runtime=c --no-project shared/tests/match/checks.ot
	./otium --runtime=chez --no-project shared/tests/match/checks.ot

.PHONY: test-library
test-library: all
	$(RUN_PYTHON) shared/tests/test_library.py
	$(RUN_PYTHON) shared/tests/test_modules.py

.PHONY: test-ui
test-ui: all
	./otium --runtime=c --no-project --path shared/lib shared/tests/ui/checks.ot
	OTIUM_GC_STRESS=1 ./otium --runtime=c --no-project --path shared/lib shared/tests/ui/checks.ot
	./otium --runtime=chez --no-project --path shared/lib shared/tests/ui/checks.ot
	./otium --runtime=c --no-project -L shared/lib -L examples shared/tests/ui/calculator-checks.ot
	OTIUM_GC_STRESS=1 ./otium --runtime=c --no-project -L shared/lib -L examples shared/tests/ui/calculator-checks.ot
	./otium --runtime=chez --no-project -L shared/lib -L examples shared/tests/ui/calculator-checks.ot
	./otium --runtime=c --no-project -L shared/lib -L examples shared/tests/ui/editing-checks.ot
	OTIUM_GC_STRESS=1 ./otium --runtime=c --no-project -L shared/lib -L examples shared/tests/ui/editing-checks.ot
	./otium --runtime=chez --no-project -L shared/lib -L examples shared/tests/ui/editing-checks.ot
	./otium --runtime=c --no-project -L shared/lib shared/tests/ui/policy-checks.ot
	OTIUM_GC_STRESS=1 ./otium --runtime=c --no-project -L shared/lib shared/tests/ui/policy-checks.ot
	./otium --runtime=chez --no-project -L shared/lib shared/tests/ui/policy-checks.ot

.PHONY: ui-setup ui test-ui-layout test-ui-gpu
ui-setup:
	$(RUN_PYTHON) shared/ui/setup.py --download

ui: ffi
	$(RUN_PYTHON) shared/ui/setup.py
	cmake -S shared/ui -B shared/build/$(shell uname -m)-$(shell uname -s | tr '[:upper:]' '[:lower:]')/ui -DCMAKE_BUILD_TYPE=Release
	cmake --build shared/build/$(shell uname -m)-$(shell uname -s | tr '[:upper:]' '[:lower:]')/ui -j 4

test-ui-layout: ui
	$(RUN_PYTHON) shared/tests/test_ui_layout.py

test-ui-gpu: ui
	$(RUN_PYTHON) shared/tests/test_ui_gpu.py --require-gpu

.PHONY: help test-tools test-native test-gpu test-benchmarks generate check-generated
help:
	@printf '%s\n' \
	  'make                         Build C (Chez runs from source)' \
	  './otium --runtime=c|chez FILE.ot -- ARGS' \
	  'make test                    Default portable + runtime + REPL tests' \
	  'make test-c / test-chez       Runtime tests and shared conditions' \
	  'make test-native             FFI, SDL, and native UI layout checks' \
	  'make test-gpu                Require GPU and accelerated SDL checks' \
	  'make test-benchmarks          Reduced R7RS correctness checks' \
	  'make test-site               Build and check browser runtime/site' \
	  'make generate / check-generated  Checked-in CLI and reference catalogs' \
	  'make ui-setup / sdl-setup     Prepare optional native dependencies' \
	  'make gc-lint                 Optional LLVM managed-pointer analysis' \
	  'See docs/development.md for dependencies and conventions.'

test-tools:
	$(RUN_PYTHON) -m unittest discover -s tools/tests -v

test-native: test-ffi test-sdl test-ui-layout

test-gpu: test-ui-gpu test-sdl-integration

test-benchmarks: all
	$(RUN_PYTHON) benchmarks/r7rs/test_ports.py --runtime c
	$(RUN_PYTHON) benchmarks/r7rs/test_ports.py --runtime chez

generate:
	$(RUN_PYTHON) shared/cli/generate.py
	$(RUN_PYTHON) shared/documentation/generate.py

check-generated:
	$(RUN_PYTHON) shared/cli/generate.py --check
	$(RUN_PYTHON) shared/documentation/generate.py --check
