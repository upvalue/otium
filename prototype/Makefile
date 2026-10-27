CC ?= cc
CFLAGS ?= -O2 -g
WARNINGS = -std=c11 -Wall -Wextra -Werror
SANITIZE = -O1 -g -fsanitize=address,undefined
LDLIBS = -pthread -ldl -lm $(shell pkg-config --libs libffi)

RUNTIME = heap.c reader.c tokens.c runtime.c compiler.c vm.c builtins.c numeric.c time.c \
  filesystem.c process.c ports.c strings.c bytevectors.c reflection.c support.c conditions.c \
  restarts.c cleanup.c params.c classes.c instances.c records.c foreign.c foreign_types.c \
  collections.c tables.c reclaim.c modules.c
HEADERS = include/otium.h heap.h reader.h internal.h foreign.h foreign_types.h \
  tests/support/test.h tests/support/helpers.h

# Each tests/NAME.c is one suite, and every suite links into build/MODE/bin/tests.
# tests/value.c is also built with the portable integer representation.
TESTS = $(basename $(notdir $(wildcard tests/*.c)))
SUITES = $(subst -,_,$(TESTS)) value_portable
SUITE_OBJECTS = $(addprefix tests/, $(addsuffix .o, $(TESTS) value-portable))
TEST_SUPPORT = tests/support/test.o tests/support/helpers.o
FIXTURES = ffi_fixture.so ffi_other.so ffi_extra.so
# Python checks that drive the CLI or a host program.
SCRIPT_TESTS = test_cli.py test_interrupt.py test_port_streams.py test_port_shutdown.py \
  test_foreign_control.py

CPPFLAGS += -Iinclude -I. -Itests/support $(shell pkg-config --cflags libffi)
CPPFLAGS += -DOTIUM_LIBRARY_DIR='"$(abspath ../lib)"'
CPPFLAGS += -DOTIUM_BOOTSTRAP_FILE='"$(CURDIR)/primitives.ot"'

.PHONY: all
all: otium-proto

# Release and sanitizer builds use the same rules under build/release and
# build/sanitize. Tests find their fixtures in the matching directory.

build/release/%.o: %.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -DOTIUM_TEST_FIXTURE_DIR='"$(CURDIR)/build/release"' $(WARNINGS) $(CFLAGS) -c $< -o $@

build/sanitize/%.o: %.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -DOTIUM_TEST_FIXTURE_DIR='"$(CURDIR)/build/sanitize"' $(WARNINGS) $(SANITIZE) -c $< -o $@

# The VM's portable switch dispatch, which GCC and Clang builds otherwise skip.
build/switch/%.o: %.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -DOTIUM_VM_SWITCH -DOTIUM_TEST_FIXTURE_DIR='"$(CURDIR)/build/release"' $(WARNINGS) $(CFLAGS) -c $< -o $@

build/%/libotium.a: $(addprefix build/%/, $(RUNTIME:.c=.o))
	$(AR) rcs $@ $^

# The runner's main, listing every suite. It is generated so that a new test
# file needs no other edit.
build/suites.c: $(wildcard tests/*.c) Makefile
	@mkdir -p $(@D)
	@{ echo '#include "test.h"'; \
	  for s in $(SUITES); do echo "extern const test_suite $${s}_suite;"; done; \
	  echo 'static const test_suite* const suites[] = {'; \
	  for s in $(SUITES); do echo "    &$${s}_suite,"; done; \
	  echo '};'; \
	  echo 'int main(int argc, char** argv) {'; \
	  echo '  return test_main(argc, argv, suites, sizeof(suites) / sizeof(*suites));'; \
	  echo '}'; } > $@

build/release/bin/tests: build/suites.c $(addprefix build/release/, $(SUITE_OBJECTS) $(TEST_SUPPORT)) build/release/libotium.a
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(WARNINGS) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/switch/bin/tests: build/suites.c $(addprefix build/switch/, $(SUITE_OBJECTS) $(TEST_SUPPORT)) build/switch/libotium.a
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(WARNINGS) $(CFLAGS) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/sanitize/bin/tests: build/suites.c $(addprefix build/sanitize/, $(SUITE_OBJECTS) $(TEST_SUPPORT)) build/sanitize/libotium.a
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(WARNINGS) $(SANITIZE) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/release/tests/value-portable.o: tests/value.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -DOTIUM_PORTABLE_INTS $(WARNINGS) $(CFLAGS) -c $< -o $@

build/switch/tests/value-portable.o: tests/value.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -DOTIUM_VM_SWITCH -DOTIUM_PORTABLE_INTS $(WARNINGS) $(CFLAGS) -c $< -o $@

build/sanitize/tests/value-portable.o: tests/value.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -DOTIUM_PORTABLE_INTS $(WARNINGS) $(SANITIZE) -c $< -o $@

# Host program for the callback abort checks in test_foreign_control.py.
build/release/bin/foreign-control: build/release/tests/hosts/foreign-control.o $(addprefix build/release/, $(TEST_SUPPORT)) build/release/libotium.a
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/sanitize/bin/foreign-control: build/sanitize/tests/hosts/foreign-control.o $(addprefix build/sanitize/, $(TEST_SUPPORT)) build/sanitize/libotium.a
	@mkdir -p $(@D)
	$(CC) $(SANITIZE) $(LDFLAGS) $^ $(LDLIBS) -o $@

build/release/ffi_fixture.so: ../tests/ffi/fixture.c
	@mkdir -p $(@D)
	$(CC) -O2 -g -fPIC -shared -pthread $< -o $@

build/release/ffi_other.so: ../tests/ffi/fixture.c
	@mkdir -p $(@D)
	$(CC) -O2 -g -fPIC -shared -pthread -DFIXTURE_ID=29 $< -o $@

build/release/ffi_extra.so: tests/fixtures/ffi_extra.c
	@mkdir -p $(@D)
	$(CC) $(WARNINGS) $(CFLAGS) -fPIC -shared $< -o $@

build/sanitize/ffi_fixture.so: ../tests/ffi/fixture.c
	@mkdir -p $(@D)
	$(CC) $(SANITIZE) -fPIC -shared -pthread $< -o $@

build/sanitize/ffi_other.so: ../tests/ffi/fixture.c
	@mkdir -p $(@D)
	$(CC) $(SANITIZE) -fPIC -shared -pthread -DFIXTURE_ID=29 $< -o $@

build/sanitize/ffi_extra.so: tests/fixtures/ffi_extra.c
	@mkdir -p $(@D)
	$(CC) $(WARNINGS) $(SANITIZE) -fPIC -shared $< -o $@

# Scripts expect the CLI next to this Makefile.
otium-proto: build/release/main.o build/release/libotium.a
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

otium-proto-sanitize: build/sanitize/main.o build/sanitize/libotium.a
	$(CC) $(SANITIZE) $(LDFLAGS) $^ $(LDLIBS) -o $@

# Run every suite, then the scripts, then the CLI tests against the
# amalgamation. The runner prints a line per suite and details of failures.
.PHONY: test sanitize
test: build/release/bin/tests $(addprefix build/release/, $(FIXTURES)) build/release/bin/foreign-control \
  otium-proto build/amalgamation/otium-proto
	@failed=0; \
	build/release/bin/tests || failed=1; \
	for s in $(SCRIPT_TESTS); do python3 $$s || failed=1; done; \
	OTIUM_TEST_CLI=build/amalgamation/otium-proto python3 test_cli.py || failed=1; \
	exit $$failed

# Leak detection is off: the tracing environment used for development breaks it.
sanitize: build/sanitize/bin/tests $(addprefix build/sanitize/, $(FIXTURES)) build/sanitize/bin/foreign-control \
  otium-proto-sanitize
	@failed=0; \
	ASAN_OPTIONS=detect_leaks=0 build/sanitize/bin/tests || failed=1; \
	for s in $(SCRIPT_TESTS); do \
	  ASAN_OPTIONS=detect_leaks=0 OTIUM_TEST_CLI=./otium-proto-sanitize \
	    OTIUM_TEST_FOREIGN=build/sanitize/bin/foreign-control python3 $$s || failed=1; \
	done; \
	exit $$failed

.PHONY: test-switch
test-switch: build/switch/bin/tests $(addprefix build/release/, $(FIXTURES))
	build/switch/bin/tests

# make test-NAME and sanitize-NAME run the suite in tests/NAME.c alone.
test-%: build/release/bin/tests $(addprefix build/release/, $(FIXTURES))
	$< $(subst -,_,$*)/

sanitize-%: build/sanitize/bin/tests $(addprefix build/sanitize/, $(FIXTURES))
	ASAN_OPTIONS=detect_leaks=0 $< $(subst -,_,$*)/

# The public header must stand alone and must not mention internal names.
.PHONY: check-public
check-public:
	$(CC) -Iinclude $(WARNINGS) -pedantic -fsyntax-only -x c include/otium.h
	! grep -n 'oti_' include/otium.h

# Execution must not need a C compiler.
.PHONY: test-no-compiler
test-no-compiler: build/release/bin/tests $(addprefix build/release/, $(FIXTURES))
	env PATH=/nonexistent build/release/bin/tests runtime/ foreign/

# The single-file distribution: otium.h, otium.c and main.c. The CLI tests run
# against a launcher built from just those files.
build/amalgamation/otium.c: tools/amalgamate.py Makefile $(RUNTIME) $(HEADERS) main.c
	python3 tools/amalgamate.py build/amalgamation

build/amalgamation/otium-proto: build/amalgamation/otium.c
	$(CC) -DOTIUM_LIBRARY_DIR='"$(abspath ../lib)"' -DOTIUM_BOOTSTRAP_FILE='"$(CURDIR)/primitives.ot"' \
	  $(shell pkg-config --cflags libffi) $(WARNINGS) $(CFLAGS) $(LDFLAGS) \
	  build/amalgamation/otium.c build/amalgamation/main.c $(LDLIBS) -o $@

.PHONY: amalgamation test-amalgamation
amalgamation: build/amalgamation/otium-proto

test-amalgamation: build/amalgamation/otium-proto
	OTIUM_TEST_CLI=build/amalgamation/otium-proto python3 test_cli.py

.PHONY: compliance differential
compliance: otium-proto
	python3 compliance.py

differential: otium-proto
	python3 differential.py

# make differential-NAME runs differential_NAME.py against the CLI.
differential-%: otium-proto build/release/ffi_fixture.so
	python3 differential_$(subst -,_,$*).py

bench_runtime: build/release/bench_runtime.o build/release/libotium.a build/release/ffi_fixture.so
	$(CC) $(LDFLAGS) build/release/bench_runtime.o build/release/libotium.a $(LDLIBS) -o $@

bench_lua: bench_lua.c
	$(CC) $(WARNINGS) $(CFLAGS) bench_lua.c -ldl -o $@

.PHONY: test-benchmark-lua
test-benchmark-lua: bench_lua
	python3 test_benchmark_lua.py

.PHONY: clean
clean:
	rm -rf build otium-proto otium-proto-sanitize bench_runtime bench_lua

.SECONDARY:
