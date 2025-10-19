#!/bin/bash
set -euo pipefail

# Source common compiler flags
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common-flags.sh"

# Emscripten compiler
CC=emcc
echo "Compiler: $(which emcc)"
emcc --version | head -1
echo ""

set -x

# Compiler flags for WASM
CPPFLAGS="$COMMON_CPPFLAGS -DOT_ARCH_WASM"
CFLAGS="$COMMON_CFLAGS"

# Emscripten-specific flags
EMFLAGS=(
  -s ASYNCIFY=1
  -s INITIAL_MEMORY=67108864  # 64MB initial
  -s ALLOW_MEMORY_GROWTH=1
  -s "EXPORTED_FUNCTIONS=[_main]"
  -s "EXPORTED_RUNTIME_METHODS=[ccall,cwrap,print]"
  -s MODULARIZE=1
  -s "EXPORT_NAME=OtiumOS"
)

# Build the complete system
$CC $CPPFLAGS $CFLAGS "${EMFLAGS[@]}" -o otk/kernel.js \
    otk/kernel.cpp \
    otk/kernel-prog.cpp \
    otk/platform-wasm.cpp \
    otk/std.cpp \
    otk/memory.cpp \
    otk/process.cpp \
    otu/user-wasm.cpp \
    otu/prog-shell.cpp \
    otu/tcl.cpp \
    otu/vendor/tlsf.c

set +x 

echo "Build complete! Output files:"
echo "  otk/kernel.js"
echo "  otk/kernel.wasm"
