include config.mk
-include site.mk

.DEFAULT_GOAL := all

CPPFLAGS += -DOT_HEAP_INIT=$(HEAP_INIT) -DOT_HEAP_MAX=$(HEAP_MAX) \
	-DOT_MAX_DEPTH=$(MAX_DEPTH) -DOT_GC_NURSERY_BYTES=$(GC_NURSERY_BYTES) \
	-DOT_GC_OLD_CHUNK_BYTES=$(GC_OLD_CHUNK_BYTES) \
	-DOT_GC_LARGE_OBJECT_BYTES=$(GC_LARGE_OBJECT_BYTES) \
	-DOT_GC_MARK_STACK_ENTRIES=$(GC_MARK_STACK_ENTRIES) -DOT_GC_TIMING=$(GC_TIMING)
CPPFLAGS += $(EXTRA_CPPFLAGS)

BUILD ?= $(BUILD_ROOT)/$(PLATFORM)
BIN := $(BIN_ROOT)/$(PLATFORM)
OTIUM := $(BIN)/otium
WITH_FFI ?= 0
OBJ := $(BUILD)/obj/$(GC)-ffi$(WITH_FFI)
GEN := $(BUILD)/gen

ifeq ($(GC),semi)
GC_OBJ := $(OBJ)/ot-gc-semi.o
CPPFLAGS += -DOT_GC_SEMI
else ifeq ($(GC),gen)
GC_OBJ := $(OBJ)/ot-gc-gen.o
CPPFLAGS += -DOT_GC_GEN
else
$(error GC must be "semi" or "gen")
endif

RUNTIME_OBJ := $(OBJ)/otium.o $(OBJ)/ot-gc-common.o $(GC_OBJ) $(OBJ)/ot-posix.o $(OBJ)/ot-ffi.o
CLI_OBJ := $(OBJ)/main.o $(OBJ)/ot-ext-demo.o $(OBJ)/line-edit.o $(OBJ)/bestline.o
TEST_OBJ := $(OBJ)/test-runtime.o
GC_BENCH_OBJ := $(OBJ)/gc-bench.o

ifeq ($(WITH_FFI),1)
CPPFLAGS += -DOT_WITH_FFI $(shell pkg-config --cflags libffi)
LDLIBS += $(shell pkg-config --libs libffi)
RUNTIME_OBJ += $(OBJ)/ffi-native.o
ifeq ($(shell uname -s),Linux)
LDLIBS += -ldl
endif
endif

.PHONY: all lib test bench bench-gc format clean hygiene gc-lint FORCE

# The public output path is shared by collector selections. Relink it so
# switching GC cannot leave a binary from the previous build.
FORCE:

all: $(OTIUM)

lib: $(BUILD)/libotium.a

$(OTIUM): $(RUNTIME_OBJ) $(CLI_OBJ) FORCE | $(BIN)
	$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)

$(BUILD)/libotium.a: $(RUNTIME_OBJ) FORCE
	$(AR) rcs $@ $(filter %.o,$^)

$(BUILD)/test-runtime: $(RUNTIME_OBJ) $(TEST_OBJ) FORCE
	$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)

$(BUILD)/gc-bench: $(RUNTIME_OBJ) $(GC_BENCH_OBJ) FORCE
	$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)

$(OBJ)/otium.o: src/otium.c src/otium.h ../shared/documentation/runtime.inc $(GEN)/expander.h $(GEN)/prelude.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(GEN) -Isrc -c $< -o $@

$(OBJ)/ot-gc-common.o: src/ot-gc-common.c src/otium.h src/ot-gc-internal.inc | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/ot-gc-semi.o: src/ot-gc-semi.c src/otium.h src/ot-gc-internal.inc src/ot-gc-trace.inc | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/ot-gc-gen.o: src/ot-gc-gen.c src/otium.h src/ot-gc-internal.inc src/ot-gc-trace.inc | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/ot-posix.o: src/ot-posix.c src/otium.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/main.o: src/main.c src/otium.h ../shared/line-edit.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -I../shared -c $< -o $@

$(OBJ)/ot-ext-demo.o: src/ot-ext-demo.c src/otium.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/test-runtime.o: tests/test_runtime.c src/otium.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/gc-bench.o: benchmarks/gc_bench.c src/otium.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/line-edit.o: ../shared/line-edit.c ../shared/line-edit.h ../shared/vendor/bestline/bestline.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I../shared/vendor/bestline -c $< -o $@

$(OBJ)/bestline.o: ../shared/vendor/bestline/bestline.c ../shared/vendor/bestline/bestline.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CSTD) $(OPT) -g -w -I../shared/vendor/bestline -c $< -o $@

$(GEN)/expander.h: prelude/expander.scm tools/embed.py | $(GEN)
	$(PYTHON) tools/embed.py $< ot_expander_src $@

$(GEN)/prelude.h: prelude/prelude.scm tools/embed.py | $(GEN)
	$(PYTHON) tools/embed.py $< ot_prelude_src $@

$(OBJ) $(GEN) $(BIN):
	mkdir -p $@

hygiene:
	$(PYTHON) tests/check_hygiene.py .
	! rg -n '^\s*#\s*define\s+OT_INTERNAL\b' --glob '!src/**' .

gc-lint:
	WITH_FFI="$(WITH_FFI)" tools/gc-lint/run

test: all $(BUILD)/test-runtime hygiene
	$(BUILD)/test-runtime
	$(PYTHON) tests/otium/run-tests.py $(OTIUM)
	$(PYTHON) tests/test_cli.py $(OTIUM) tests/cli-load.scm tests/cli-load-second.scm
	$(OTIUM) --no-project --path ../examples/c/demo tests/ext-demo.scm
	$(OTIUM) --no-project --path ../examples/c/otcl tests/otcl.scm

bench: all
	$(PYTHON) ../benchmarks/run.py $(OTIUM)

bench-gc: $(BUILD)/gc-bench
	$(BUILD)/gc-bench churn
	$(BUILD)/gc-bench mixed
	$(BUILD)/gc-bench fragmentation

format:
	tools/format-c

clean:
	rm -rf $(BUILD)
	rm -rf $(OTIUM)

$(OBJ)/ot-ffi.o: src/ot-ffi.c src/otium.h ../shared/ffi/native.h $(GEN)/ot-ffi-source.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(GEN) -Isrc -c $< -o $@

$(OBJ)/ffi-native.o: ../shared/ffi/native.c ../shared/ffi/native.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(GEN)/ot-ffi-source.h: ../shared/ffi/core.scm tools/embed.py | $(GEN)
	$(PYTHON) tools/embed.py $< ot_ffi_src $@
