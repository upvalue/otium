.DEFAULT_GOAL := all
CC ?= cc
CMAKE ?= cmake
PLATFORM ?= $(shell uname -m)-$(shell uname -s | tr '[:upper:]' '[:lower:]')
BUILD ?= build/$(PLATFORM)
CFLAGS ?= -O2 -g
CXXFLAGS ?= -O2 -g
CPPFLAGS ?=
LDFLAGS ?=
WITH_UI ?= OFF
CMAKE_BUILD := $(BUILD)/cmake
EXT := $(if $(filter Darwin,$(shell uname -s)),dylib,so)
quote = '$(subst ','"'"',$(1))'

.PHONY: all configure ffi ui native-objects test clean line dev
configure:
	+@$(CMAKE) -S . -B $(CMAKE_BUILD) \
	  -DOTIUM_OUTPUT_DIR=$(abspath $(BUILD)) -DOTIUM_WITH_UI=$(WITH_UI) \
	  -DCMAKE_C_COMPILER=$(CC) -DCMAKE_C_FLAGS=$(call quote,$(CPPFLAGS) $(CFLAGS)) \
	  -DCMAKE_CXX_FLAGS=$(call quote,$(CPPFLAGS) $(CXXFLAGS)) \
	  -DCMAKE_SHARED_LINKER_FLAGS=$(call quote,$(LDFLAGS)) \
	  -DCMAKE_EXE_LINKER_FLAGS=$(call quote,$(LDFLAGS)) > /dev/null

all: configure
	+@$(CMAKE) --build $(CMAKE_BUILD) > /dev/null

ffi line dev native-objects: configure
	+@$(CMAKE) --build $(CMAKE_BUILD) --target $(if $(filter native-objects,$@),native-objects,otium-$@) > /dev/null

ui: WITH_UI=ON
ui: CMAKE_BUILD=$(BUILD)/cmake-ui
ui: configure
	+@$(CMAKE) --build $(CMAKE_BUILD) --target otium-ui otium-ui-caret-checks otium-ui-text-reference otium-ui-test-events > /dev/null

test: all
	ctest --test-dir $(CMAKE_BUILD) --output-on-failure

# Preserve the artifact entry points used by launchers and external callers.
.PHONY: $(BUILD)/libotium-line.$(EXT) $(BUILD)/libotium-dev.$(EXT) $(BUILD)/libotium-ffi.$(EXT) $(BUILD)/libotium-line.a
$(BUILD)/libotium-line.$(EXT): line
$(BUILD)/libotium-dev.$(EXT): dev
$(BUILD)/libotium-ffi.$(EXT): ffi
$(BUILD)/libotium-line.a: configure
	+@$(CMAKE) --build $(CMAKE_BUILD) --target otium-line-static > /dev/null

clean:
	rm -rf $(BUILD)
