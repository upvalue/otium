#!/bin/bash
set -euo pipefail

# Check if emcc is available
if ! command -v emcc &> /dev/null; then
    echo "Error: emcc (Emscripten) is not installed or not in PATH"
    echo ""
    echo "To install Emscripten:"
    echo "  git clone https://github.com/emscripten-core/emsdk.git"
    echo "  cd emsdk"
    echo "  ./emsdk install latest"
    echo "  ./emsdk activate latest"
    echo "  source ./emsdk_env.sh"
    echo ""
    echo "Or visit: https://emscripten.org/docs/getting_started/downloads.html"
    exit 1
fi

# Parse arguments
TEST_PROG=""
VERBOSE=""

for arg in "$@"; do
  case $arg in
    --test-hello)
      TEST_PROG="-DKERNEL_PROG_TEST_HELLO"
      ;;
    --test-mem)
      TEST_PROG="-DKERNEL_PROG_TEST_MEM"
      ;;
    --test-alternate)
      TEST_PROG="-DKERNEL_PROG_TEST_ALTERNATE"
      ;;
    -v|--verbose)
      VERBOSE="yes"
      set -x
      ;;
  esac
done

echo "=== Otium OS WASM Build ==="
echo ""

# Emscripten compiler
CC=emcc
echo "Compiler: $(which emcc)"
emcc --version | head -1
echo ""

# Compiler flags for WASM
CPPFLAGS="-I. -DOT_ARCH_WASM -DOT_TRACE_MEM $TEST_PROG"
CFLAGS="-O2 -g3 -Wall -Wextra -fno-exceptions -fno-rtti"

if [ -n "$TEST_PROG" ]; then
  echo "Building with test mode: $TEST_PROG"
else
  echo "Building default mode (shell)"
fi
echo ""

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

echo ""
echo "Build complete! Output files:"
echo "  otk/kernel.js"
echo "  otk/kernel.wasm"
