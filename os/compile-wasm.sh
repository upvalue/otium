#!/bin/bash
set -euo pipefail

# Source common compiler flags and source lists
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build-common.sh"

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
mkdir -p bin
$CC $CPPFLAGS $CFLAGS "${EMFLAGS[@]}" -o bin/kernel.js \
    "${KERNEL_SHARED_SOURCES[@]}" \
    ot/kernel/platform-wasm.cpp \
    ot/user/user-wasm.cpp \
    "${COMMON_SHARED_SOURCES[@]}" \
    "${USER_SHARED_SOURCES[@]}"

set +x

echo "Build complete! Output files:"
echo "  bin/kernel.js"
echo "  bin/kernel.wasm"
