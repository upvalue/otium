.DEFAULT_GOAL := all

.PHONY: all lib test test-c test-chez test-sdl sdl-setup bench bench-gc format clean hygiene gc-lint

# Keep the existing C entry points; implementation rules live with the runtime.
all lib bench bench-gc format clean hygiene gc-lint:
	$(MAKE) -C c $@

test: test-c test-chez

test-c:
	$(MAKE) -C c test
	python3 shared/tests/test_conditions.py --runtime c

test-chez:
	chez/test
	python3 shared/tests/test_conditions.py --runtime chez

sdl-setup:
	python3 chez/sdl/setup.py

test-sdl:
	python3 chez/sdl/test_live.py
