# Included by shared/Makefile for explicit UI builds only.
PKG_CONFIG ?= pkg-config
GLSLC ?= glslc
YOGA_ROOT ?= build/deps/yoga-3.2.1
CONFIG_DIR ?= build
UI_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl3 sdl3-ttf 2>/dev/null)
UI_LIBS := $(shell $(PKG_CONFIG) --libs sdl3 sdl3-ttf 2>/dev/null)
SDL_LIBRARY ?= $(shell $(PKG_CONFIG) --variable=libdir sdl3)/libSDL3.$(EXT)
UI_OBJ := $(BUILD)/ui/objects
UI_GEN := $(BUILD)/ui/generated
UI_CPPFLAGS := $(UI_CFLAGS) -I$(YOGA_ROOT) -I$(UI_GEN) -DOTUI_FONT_DIR=$(call quote,"$(abspath ui/fonts)")
YOGA_SOURCES := $(sort $(wildcard $(YOGA_ROOT)/yoga/*.cpp $(YOGA_ROOT)/yoga/*/*.cpp))
YOGA_OBJECTS := $(patsubst $(YOGA_ROOT)/%.cpp,$(UI_OBJ)/%.o,$(YOGA_SOURCES))
UI_OBJECTS := $(addprefix $(UI_OBJ)/,layout.o renderer.o text.o)
UI_TEST_OBJECTS := $(addprefix $(UI_OBJ)/,text-reference.o caret-checks.o events.o)

.PHONY: ui ui-dependencies
ui: $(BUILD)/libotium-ui.$(EXT) $(BUILD)/ui/otium-ui-text-reference $(BUILD)/ui/otium-ui-caret-checks $(BUILD)/libotium-ui-test-events.$(EXT)
	$(PYTHON) ui/write-config.py $(call quote,$(CONFIG_DIR)) $(call quote,$(abspath $(BUILD)/libotium-ui.$(EXT))) \
	  $(call quote,$(abspath $(BUILD)/libotium-ui-test-events.$(EXT))) $(call quote,$(SDL_LIBRARY))

ui-dependencies:
	@$(PKG_CONFIG) --print-errors --exists 'sdl3 >= 3.4.12' 'sdl3-ttf >= 3.2.2'
	@test -f $(YOGA_ROOT)/yoga/Yoga.h || { echo 'Yoga is missing; run make ui-setup' >&2; exit 1; }
	@test -f $(call quote,$(SDL_LIBRARY)) || { echo 'SDL library is missing; set SDL_LIBRARY to its path' >&2; exit 1; }
	@$(GLSLC) --version > /dev/null

$(BUILD)/.ui-config: FORCE | ui-dependencies
	@mkdir -p $(dir $@)
	@temp=$@.$$$$; printf '%s\n' $(call quote,$(CC)|$(CXX)|$(AR)|$(CPPFLAGS)|$(CFLAGS)|$(CXXFLAGS)|$(LDFLAGS)|$(LDLIBS)|$(UI_CPPFLAGS)|$(UI_LIBS)|$(GLSLC)|$(YOGA_SOURCES)) > "$$temp"; \
	  cmp -s "$$temp" $@ || mv "$$temp" $@; rm -f "$$temp"

$(UI_OBJECTS) $(UI_TEST_OBJECTS) $(YOGA_OBJECTS): Makefile ui/build.mk $(BUILD)/.ui-config

$(UI_OBJ)/%.o: ui/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) $(WARN) $(UI_CPPFLAGS) -fPIC -MMD -MP -c $< -o $@
$(UI_OBJ)/layout.o: ui/layout.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++20 $(CPPFLAGS) $(CXXFLAGS) $(WARN) $(UI_CPPFLAGS) -fPIC -MMD -MP -c $< -o $@
$(UI_OBJ)/yoga/%.o: $(YOGA_ROOT)/yoga/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) -std=c++20 $(CPPFLAGS) $(CXXFLAGS) -Wall -Werror -fno-omit-frame-pointer -fexceptions -fno-rtti -fvisibility=hidden -fPIC -MMD -MP -I$(YOGA_ROOT) -c $< -o $@
$(UI_OBJ)/%.o: tests/ui/%.c
	@mkdir -p $(dir $@)
	$(CC) -std=c11 $(CPPFLAGS) $(CFLAGS) $(WARN) $(UI_CPPFLAGS) -fPIC -MMD -MP -c $< -o $@

$(UI_GEN)/shaders.h: ui/shaders/embed.py ui/shaders/rect.metal ui/shaders/rect.vert ui/shaders/rect.frag $(BUILD)/.ui-config
	@mkdir -p $(dir $@)
	$(PYTHON) ui/shaders/embed.py ui/shaders $@ $(call quote,$(GLSLC))
$(UI_OBJ)/renderer.o: $(UI_GEN)/shaders.h

$(BUILD)/ui/libyoga.a: $(YOGA_OBJECTS) Makefile ui/build.mk $(BUILD)/.ui-config
	rm -f $@.tmp
	$(AR) rcs $@.tmp $(YOGA_OBJECTS)
	mv $@.tmp $@
$(BUILD)/libotium-ui.$(EXT): $(UI_OBJECTS) $(BUILD)/ui/libyoga.a Makefile ui/build.mk $(BUILD)/.ui-config
	$(CXX) $(LDFLAGS) $(SHARED_FLAGS) $(LIB_ID) -o $@.tmp $(UI_OBJECTS) $(BUILD)/ui/libyoga.a $(UI_LIBS) -lm $(LDLIBS)
	mv $@.tmp $@
$(BUILD)/libotium-ui-test-events.$(EXT): $(UI_OBJ)/events.o Makefile ui/build.mk $(BUILD)/.ui-config
	$(CC) $(LDFLAGS) $(SHARED_FLAGS) $(LIB_ID) -o $@.tmp $(UI_OBJ)/events.o $(UI_LIBS) $(LDLIBS)
	mv $@.tmp $@
$(BUILD)/ui/otium-ui-text-reference: $(UI_OBJ)/text-reference.o Makefile ui/build.mk $(BUILD)/.ui-config
	$(CC) $(LDFLAGS) -o $@.tmp $(UI_OBJ)/text-reference.o $(UI_LIBS) $(LDLIBS)
	mv $@.tmp $@
$(BUILD)/ui/otium-ui-caret-checks: $(UI_OBJ)/caret-checks.o $(BUILD)/libotium-ui.$(EXT) Makefile ui/build.mk $(BUILD)/.ui-config
	$(CC) $(LDFLAGS) -o $@.tmp $(UI_OBJ)/caret-checks.o $(abspath $(BUILD)/libotium-ui.$(EXT)) -Wl,-rpath,$(abspath $(BUILD)) $(UI_LIBS) $(LDLIBS)
	mv $@.tmp $@

-include $(UI_OBJECTS:.o=.d) $(UI_TEST_OBJECTS:.o=.d) $(YOGA_OBJECTS:.o=.d)
