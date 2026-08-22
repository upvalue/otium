include config.mk
-include site.mk

CPPFLAGS += -DOT_HEAP_INIT=$(HEAP_INIT) -DOT_HEAP_MAX=$(HEAP_MAX) \
	-DOT_MAX_DEPTH=$(MAX_DEPTH) -DOT_GC_NURSERY_BYTES=$(GC_NURSERY_BYTES) \
	-DOT_GC_OLD_CHUNK_BYTES=$(GC_OLD_CHUNK_BYTES) \
	-DOT_GC_LARGE_OBJECT_BYTES=$(GC_LARGE_OBJECT_BYTES) \
	-DOT_GC_MARK_STACK_ENTRIES=$(GC_MARK_STACK_ENTRIES) -DOT_GC_TIMING=$(GC_TIMING) \
	-DOT_GSGC_MAX_AGE=$(GSGC_MAX_AGE) -DOT_GSGC_MAX_REMEMBERED=$(GSGC_MAX_REMEMBERED) \
	-DOT_GSGC_MIN_NEW_SPACE=$(GSGC_MIN_NEW_SPACE)
CPPFLAGS += $(EXTRA_CPPFLAGS)

BUILD ?= build
OBJ := $(BUILD)/obj/$(GC)
GEN := $(BUILD)/gen

ifeq ($(GC),semi)
GC_OBJ := $(OBJ)/ot-gc-semi.o
CPPFLAGS += -DOT_GC_SEMI
else ifeq ($(GC),gen)
GC_OBJ := $(OBJ)/ot-gc-gen.o
CPPFLAGS += -DOT_GC_GEN
else ifeq ($(GC),gsgc)
GC_OBJ := $(OBJ)/ot-gc-gsgc.o
CPPFLAGS += -DOT_GC_GSGC
else
$(error GC must be "semi", "gen", or "gsgc")
endif

RUNTIME_OBJ := $(OBJ)/otium.o $(OBJ)/ot-gc-common.o $(GC_OBJ) $(OBJ)/ot-posix.o
CLI_OBJ := $(OBJ)/main.o $(OBJ)/ot-ext-demo.o $(OBJ)/bestline.o
TEST_OBJ := $(OBJ)/test-runtime.o
GC_BENCH_OBJ := $(OBJ)/gc-bench.o

ifeq ($(WITH_RAY),1)
CLI_OBJ += $(OBJ)/ot-ext-ray.o
CPPFLAGS += -DOT_WITH_RAY $(RAYLIB_CFLAGS)
LDLIBS += $(RAYLIB_LIBS)
endif

.PHONY: all lib test bench bench-gc format clean hygiene FORCE

# The public output path is shared by collector selections. Relink it so
# switching GC cannot leave a binary from the previous build.
FORCE:

all: $(BUILD)/otium

lib: $(BUILD)/libotium.a

$(BUILD)/otium: $(RUNTIME_OBJ) $(CLI_OBJ) FORCE
	$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)

$(BUILD)/libotium.a: $(RUNTIME_OBJ) FORCE
	$(AR) rcs $@ $(filter %.o,$^)

$(BUILD)/test-runtime: $(RUNTIME_OBJ) $(TEST_OBJ) FORCE
	$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)

$(BUILD)/gc-bench: $(RUNTIME_OBJ) $(GC_BENCH_OBJ) FORCE
	$(CC) $(LDFLAGS) -o $@ $(filter %.o,$^) $(LDLIBS)

$(OBJ)/otium.o: src/otium.c src/otium.h $(GEN)/expander.h $(GEN)/prelude.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(GEN) -Isrc -c $< -o $@

$(OBJ)/ot-gc-common.o: src/ot-gc-common.c src/otium.h src/ot-gc-internal.inc | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/ot-gc-semi.o: src/ot-gc-semi.c src/otium.h src/ot-gc-internal.inc src/ot-gc-trace.inc | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/ot-gc-gen.o: src/ot-gc-gen.c src/otium.h src/ot-gc-internal.inc src/ot-gc-trace.inc | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/ot-gc-gsgc.o: src/ot-gc-gsgc.c src/otium.h src/ot-gc-internal.inc src/ot-gc-trace.inc | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/ot-posix.o: src/ot-posix.c src/otium.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/main.o: src/main.c src/otium.h vendor/bestline/bestline.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -Ivendor/bestline -c $< -o $@

$(OBJ)/ot-ext-demo.o: src/ot-ext-demo.c src/otium.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/ot-ext-ray.o: src/ot-ext-ray.c src/otium.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/test-runtime.o: tests/test_runtime.c src/otium.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/gc-bench.o: benchmarks/gc_bench.c src/otium.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -Isrc -c $< -o $@

$(OBJ)/bestline.o: vendor/bestline/bestline.c vendor/bestline/bestline.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CSTD) $(OPT) -g -w -Ivendor/bestline -c $< -o $@

$(GEN)/expander.h: prelude/expander.scm tools/embed.py | $(GEN)
	$(PYTHON) tools/embed.py $< ot_expander_src $@

$(GEN)/prelude.h: prelude/prelude.scm tools/embed.py | $(GEN)
	$(PYTHON) tools/embed.py $< ot_prelude_src $@

$(OBJ) $(GEN):
	mkdir -p $@

hygiene:
	$(PYTHON) tests/check_hygiene.py .
	! rg -n '^\s*#\s*define\s+OT_INTERNAL\b' --glob '!src/**' .

test: all $(BUILD)/test-runtime hygiene
	$(BUILD)/test-runtime
	$(PYTHON) tests/otium/run-tests.py $(BUILD)/otium
	$(PYTHON) tests/test_cli.py $(BUILD)/otium tests/cli-load.scm tests/cli-load-second.scm
	$(BUILD)/otium --no-project --path examples/demo tests/ext-demo.scm
	$(BUILD)/otium --no-project --path examples/otcl tests/otcl.scm
ifeq ($(WITH_RAY),1)
	$(BUILD)/otium --no-project --path examples/ray tests/ext-ray.scm
endif

bench: all
	$(PYTHON) benchmarks/run.py $(BUILD)/otium

bench-gc: $(BUILD)/gc-bench
	$(BUILD)/gc-bench churn
	$(BUILD)/gc-bench mixed
	$(BUILD)/gc-bench fragmentation

format:
	tools/format-c

clean:
	rm -r $(BUILD)
