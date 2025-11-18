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

# WASM only supports test backend (no VirtIO hardware access)
# Graphics backend sources are included in USER_SOURCES via build-common.sh
GRAPHICS_SOURCES=()

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

# Build the complete OS (kernel + user programs in single executable)
mkdir -p bin
$CC $CPPFLAGS $CFLAGS "${EMFLAGS[@]}" -o bin/os.js \
    "${CORE_SOURCES[@]}" \
    "${DRIVER_SOURCES[@]}" \
    "${LIB_SOURCES[@]}" \
    "${USER_SOURCES[@]}" \
    ot/core/platform/platform-wasm.cpp \
    ot/lib/platform/shared-wasm.cpp \
    ot/core/platform/user-wasm.cpp

set +x

echo "Build complete! Output files:"
echo "  bin/os.js"
echo "  bin/os.wasm"
