#!/usr/bin/env bash
# Run WASM kernel in Node.js
# This script reads configuration from the Meson build directory

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="${1:-$PROJECT_ROOT/build-wasm}"

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory not found: $BUILD_DIR"
    echo "Usage: $0 [build-directory]"
    echo "Example: $0 build-wasm"
    exit 1
fi

# Check if binary exists
WASM_JS="$BUILD_DIR/os.js"
WASM_FILE="$BUILD_DIR/os.wasm"
if [ ! -f "$WASM_JS" ] || [ ! -f "$WASM_FILE" ]; then
    echo "Error: WASM files not found: $WASM_JS and $WASM_FILE"
    echo "Please build the kernel first with: meson compile -C $BUILD_DIR"
    exit 1
fi

# Check if run-wasm.js exists in project root
RUN_WASM_JS="$PROJECT_ROOT/run-wasm.js"
if [ ! -f "$RUN_WASM_JS" ]; then
    echo "Error: Runner not found: $RUN_WASM_JS"
    exit 1
fi

# Run WASM in Node.js
# Note: We need to update run-wasm.js to accept build directory as parameter
cd "$PROJECT_ROOT"
exec node "$RUN_WASM_JS" "$BUILD_DIR"
