.DEFAULT_GOAL := all
CC ?= cc
AR ?= ar
PLATFORM ?= $(shell uname -m)-$(shell uname -s | tr '[:upper:]' '[:lower:]')
BUILD ?= build/$(PLATFORM)
CFLAGS ?= -O2 -g -Wall -Wextra -Werror
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
SHARED_FLAGS := -dynamiclib
EXT := dylib
else
SHARED_FLAGS := -shared
EXT := so
endif

.PHONY: all clean
all: $(BUILD)/libotium-line.$(EXT) $(BUILD)/libotium-line.a

$(BUILD):
	mkdir -p $@

$(BUILD)/line-edit.o: line-edit.c line-edit.h vendor/bestline/bestline.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -fPIC -Ivendor/bestline -c $< -o $@

$(BUILD)/bestline.o: vendor/bestline/bestline.c vendor/bestline/bestline.h | $(BUILD)
	$(CC) $(CPPFLAGS) -O2 -g -std=c11 -fPIC -w -c $< -o $@

$(BUILD)/libotium-line.a: $(BUILD)/line-edit.o $(BUILD)/bestline.o
	$(AR) rcs $@ $^

$(BUILD)/libotium-line.$(EXT): $(BUILD)/line-edit.o $(BUILD)/bestline.o
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) -o $@ $^

clean:
	rm -rf $(BUILD)

.PHONY: ffi
ffi: $(BUILD)/libotium-ffi.$(EXT)

$(BUILD)/libotium-ffi.$(EXT): ffi/native.c ffi/native.h | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -fPIC $(SHARED_FLAGS) -o $@ ffi/native.c $(if $(filter Linux,$(UNAME)),-ldl,)
