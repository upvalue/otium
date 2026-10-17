.DEFAULT_GOAL := all

.PHONY: clean all lib test test-c test-chez test-sdl test-sdl-integration test-repl sdl-setup bench bench-gc format hygiene gc-lint

# Keep the existing C entry points; implementation rules live with the runtime.
all lib bench bench-gc format hygiene gc-lint:
	$(MAKE) -C c $@

test: test-c test-chez
	$(MAKE) test-repl

test-c:
	$(MAKE) -C c test
	python3 shared/tests/test_conditions.py --runtime c

test-chez:
	chez/test
	python3 shared/tests/test_conditions.py --runtime chez

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
