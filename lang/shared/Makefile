.DEFAULT_GOAL := all
.DELETE_ON_ERROR:
CC ?= cc
CXX ?= c++
AR ?= ar
PYTHON ?= python3
PLATFORM ?= $(shell uname -m)-$(shell uname -s | tr '[:upper:]' '[:lower:]')
BUILD ?= build/$(PLATFORM)
CFLAGS ?= -O2 -g
CXXFLAGS ?= -O2 -g
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?=
WARN := -Wall -Wextra -Werror
UNAME := $(shell uname -s)
ifeq ($(UNAME),Darwin)
EXT := dylib
SHARED_FLAGS := -dynamiclib
LIB_ID = -Wl,-install_name,$(abspath $@)
else
EXT := so
SHARED_FLAGS := -shared
LIB_ID = -Wl,-soname,$(notdir $@)
endif
DL_LIBS := $(if $(filter Linux,$(UNAME)),-ldl,)
quote = '$(subst ','"'"',$(1))'
OBJECTS := $(addprefix $(BUILD)/objects/,utf8.o ffi-native.o dev-transport.o line-edit.o bestline.o)
LINE_OBJECTS := $(BUILD)/objects/line-edit.o $(BUILD)/objects/bestline.o
DEV_OBJECTS := $(BUILD)/objects/dev-transport.o $(BUILD)/objects/utf8.o
FFI_OBJECTS := $(BUILD)/objects/ffi-native.o $(BUILD)/objects/utf8.o

.PHONY: all ffi line dev native-objects test clean FORCE
all: line dev $(BUILD)/libotium-line.a $(BUILD)/otium-utf8-checks
line: $(BUILD)/libotium-line.$(EXT)
dev: $(BUILD)/libotium-dev.$(EXT)
ffi: $(BUILD)/libotium-ffi.$(EXT)
native-objects: $(OBJECTS)
FORCE:

# Update the stamp only when the toolchain/flags change, including a switch back
# to an earlier configuration. Headers are tracked by compiler-generated .d files.
$(BUILD)/.native-config: FORCE
	@mkdir -p $(dir $@)
	@temp=$@.$$$$; printf '%s\n' $(call quote,$(CC)|$(AR)|$(CPPFLAGS)|$(CFLAGS)|$(LDFLAGS)|$(LDLIBS)|$(SHARED_FLAGS)) > "$$temp"; \
	  cmp -s "$$temp" $@ || mv "$$temp" $@; rm -f "$$temp"

$(OBJECTS) $(BUILD)/objects/utf8-checks.o: Makefile $(BUILD)/.native-config

$(BUILD)/objects/utf8.o: support/utf8.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) $(WARN) -fPIC -MMD -MP -c $< -o $@
$(BUILD)/objects/ffi-native.o: ffi/native.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) $(WARN) -fPIC -MMD -MP -c $< -o $@
$(BUILD)/objects/dev-transport.o: dev/transport.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) $(WARN) -fPIC -MMD -MP -c $< -o $@
$(BUILD)/objects/line-edit.o: line-edit.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) $(WARN) -fPIC -MMD -MP -Ivendor/bestline -c $< -o $@
$(BUILD)/objects/bestline.o: vendor/bestline/bestline.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -w -fPIC -MMD -MP -c $< -o $@
$(BUILD)/objects/utf8-checks.o: tests/native/utf8.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) $(WARN) -MMD -MP -Isupport -c $< -o $@

$(BUILD)/libotium-line.$(EXT): $(LINE_OBJECTS) Makefile $(BUILD)/.native-config
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) $(LIB_ID) -o $@.tmp $(LINE_OBJECTS) $(LDLIBS)
	mv $@.tmp $@
$(BUILD)/libotium-dev.$(EXT): $(DEV_OBJECTS) Makefile $(BUILD)/.native-config
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) $(LIB_ID) -o $@.tmp $(DEV_OBJECTS) $(LDLIBS)
	mv $@.tmp $@
$(BUILD)/libotium-ffi.$(EXT): $(FFI_OBJECTS) Makefile $(BUILD)/.native-config
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) $(LIB_ID) -o $@.tmp $(FFI_OBJECTS) $(DL_LIBS) $(LDLIBS)
	mv $@.tmp $@
$(BUILD)/libotium-line.a: $(LINE_OBJECTS) Makefile $(BUILD)/.native-config
	rm -f $@.tmp
	$(AR) rcs $@.tmp $(LINE_OBJECTS)
	mv $@.tmp $@
$(BUILD)/otium-utf8-checks: $(BUILD)/objects/utf8-checks.o $(BUILD)/objects/utf8.o Makefile $(BUILD)/.native-config
	$(CC) $(LDFLAGS) -o $@.tmp $(filter %.o,$^) $(LDLIBS)
	mv $@.tmp $@

test: $(BUILD)/otium-utf8-checks
	$(BUILD)/otium-utf8-checks

clean:
	rm -rf $(BUILD)

-include $(OBJECTS:.o=.d) $(BUILD)/objects/utf8-checks.d

# Optional dependencies are discovered only for an explicit UI target.
ifneq ($(filter ui $(BUILD)/libotium-ui.$(EXT) $(BUILD)/libotium-ui-test-events.$(EXT) $(BUILD)/ui/%,$(MAKECMDGOALS)),)
include ui/build.mk
endif
