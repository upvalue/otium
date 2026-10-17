# ✨ Standalone browser build. Native build outputs are kept separate.
EMCC ?= emcc
PYTHON ?= python3
OUT ?= ../../site/public/assets
BUILD ?= ../../build/wasm
SOURCES := ../src/otium.c ../src/ot-gc-common.c ../src/ot-gc-semi.c ../src/ot-posix.c ../src/ot-ffi.c ../src/ot-cli.c ../src/ot-wasm.c
HEADERS := $(BUILD)/expander.h $(BUILD)/prelude.h $(BUILD)/extra.h $(BUILD)/ot-ffi-source.h $(BUILD)/ot-cli-source.h
FLAGS := -std=c23 -O2 -DOT_GC_SEMI -DOT_HEAP_INIT=1048576 -DOT_HEAP_MAX=33554432 -DOT_MAX_DEPTH=20000

.PHONY: all
all: $(OUT)/otium.mjs

$(OUT)/otium.mjs: $(SOURCES) ../src/otium.h ../src/ot-gc-internal.inc ../src/ot-gc-trace.inc ../src/ot-classes.inc ../src/ot-methods.inc ../../shared/documentation/runtime.inc $(HEADERS)
	mkdir -p $(OUT)
	$(EMCC) $(FLAGS) -I../src -I$(BUILD) $(SOURCES) -lm --no-entry \
	  -sMODULARIZE=1 -sEXPORT_ES6=1 -sENVIRONMENT=web,worker,node \
	  -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=268435456 -sSTACK_SIZE=1048576 \
	  -sEXPORTED_FUNCTIONS='["_otium_web_init","_otium_web_eval"]' \
	  -sEXPORTED_RUNTIME_METHODS='["ccall"]' -o $@

$(BUILD):
	mkdir -p $@
$(BUILD)/expander.h: ../../shared/prelude/expander.ot ../tools/embed.py | $(BUILD)
	$(PYTHON) ../tools/embed.py $< ot_expander_src $@
$(BUILD)/prelude.h: ../../shared/prelude/core.ot ../tools/embed.py | $(BUILD)
	$(PYTHON) ../tools/embed.py $< ot_prelude_src $@
$(BUILD)/ot-ffi-source.h: ../../shared/ffi/core.ot ../tools/embed.py | $(BUILD)
	$(PYTHON) ../tools/embed.py $< ot_ffi_src $@
$(BUILD)/ot-cli-source.h: ../../shared/cli/core.ot ../tools/embed.py | $(BUILD)
	$(PYTHON) ../tools/embed.py $< ot_cli_src $@

$(BUILD)/extra.h: ../../shared/lib/otium/extra.ot ../tools/embed.py | $(BUILD)
	$(PYTHON) ../tools/embed.py $< ot_extra_src $@
