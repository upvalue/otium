include config.mk
-include site.mk

CPPFLAGS += -DOT_HEAP_INIT=$(HEAP_INIT) -DOT_HEAP_MAX=$(HEAP_MAX) \
	-DOT_MAX_DEPTH=$(MAX_DEPTH)

BUILD := build
OBJ := $(BUILD)/obj
GEN := $(BUILD)/gen

RUNTIME_OBJ := $(OBJ)/otium.o $(OBJ)/ot-gc.o $(OBJ)/ot-posix.o
CLI_OBJ := $(OBJ)/main.o $(OBJ)/ot-ext-demo.o $(OBJ)/bestline.o
TEST_OBJ := $(OBJ)/test-runtime.o

ifeq ($(WITH_RAY),1)
CLI_OBJ += $(OBJ)/ot-ext-ray.o
CPPFLAGS += -DOT_WITH_RAY $(RAYLIB_CFLAGS)
LDLIBS += $(RAYLIB_LIBS)
endif

.PHONY: all lib test bench format clean hygiene

all: $(BUILD)/otium

lib: $(BUILD)/libotium.a

$(BUILD)/otium: $(RUNTIME_OBJ) $(CLI_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(BUILD)/libotium.a: $(RUNTIME_OBJ)
	$(AR) rcs $@ $^

$(BUILD)/test-runtime: $(RUNTIME_OBJ) $(TEST_OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

$(OBJ)/otium.o: src/otium.c src/otium.h $(GEN)/expander.h $(GEN)/prelude.h | $(OBJ)
	$(CC) $(CPPFLAGS) $(CFLAGS) -I$(GEN) -Isrc -c $< -o $@

$(OBJ)/ot-gc.o: src/ot-gc.c src/otium.h | $(OBJ)
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
ifeq ($(WITH_RAY),1)
	$(BUILD)/otium --no-project --path examples/ray tests/ext-ray.scm
endif

bench: all
	$(PYTHON) benchmarks/run.py $(BUILD)/otium

format:
	tools/format-c

clean:
	rm -r $(BUILD)
