.DEFAULT_GOAL := all

.PHONY: clean all lib test test-c test-chez test-expander test-sdl test-sdl-integration test-repl sdl-setup bench bench-gc format hygiene gc-lint

# Keep the existing C entry points; implementation rules live with the runtime.
all lib bench bench-gc format hygiene gc-lint:
	$(MAKE) -C c $@

test: test-c test-chez test-expander test-classes test-cli test-reader test-library
	$(MAKE) test-repl

test-c:
	$(MAKE) -C c test
	python3 shared/tests/test_conditions.py --runtime c

test-chez:
	chez/test
	python3 shared/tests/test_conditions.py --runtime chez

# One set of expander checks, run unchanged on both runtimes.
test-expander: all
	c/otium --no-project shared/tests/expander/checks.scm
	OTIUM_GC_STRESS=1 c/otium --no-project shared/tests/expander/checks.scm
	chez/run --no-project shared/tests/expander/checks.scm

sdl-setup:
	python3 shared/sdl/setup.py

test-sdl: ffi
	python3 shared/tests/test_sdl.py --runtime both

test-sdl-integration: ffi
	python3 shared/tests/test_sdl.py --runtime both --require-accelerated

test-repl: all
	$(MAKE) -C shared
	python3 shared/tests/test_help.py
	python3 shared/tests/test_repl.py

clean:
	$(MAKE) -C c clean
	$(MAKE) -C shared clean

.PHONY: test-ffi ffi
ffi:
	$(MAKE) -C c WITH_FFI=1 all
	$(MAKE) -C shared ffi

test-ffi: ffi
	python3 shared/tests/test_ffi.py --stress

.PHONY: test-cli
test-cli: all
	python3 shared/cli/generate.py --check
	python3 shared/tests/test_cli.py

.PHONY: test-reader
test-reader: all
	python3 shared/tests/test_reader.py
	python3 shared/tests/test_reader.py --stress

# ✨ Markdown documentation and the browser C runtime.
.PHONY: site test-site
site:
	npm --prefix site run build

test-site: site
	node site/tests/runtime.mjs
	$(or $(PYTHON),python3) site/tests/check_site.py

.PHONY: test-classes
test-classes: all
	c/otium --no-project shared/tests/classes/checks.scm
	OTIUM_GC_STRESS=1 c/otium --no-project shared/tests/classes/checks.scm
	chez/run --no-project shared/tests/classes/checks.scm

.PHONY: test-library
test-library: all
	python3 shared/tests/test_library.py
	python3 shared/tests/test_modules.py
