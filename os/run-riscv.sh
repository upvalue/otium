#!/bin/bash
set -euxo pipefail

QEMU=qemu-system-riscv32

# Build the OS
./compile-riscv.sh "$@"

# Source runtime configuration to check graphics backend
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -f "$SCRIPT_DIR/build/runtime-config.sh" ]; then
  source "$SCRIPT_DIR/build/runtime-config.sh"
fi

# Configure display based on graphics backend
if [ "${RUNTIME_GRAPHICS_BACKEND:-OT_GRAPHICS_BACKEND_TEST}" = "OT_GRAPHICS_BACKEND_NONE" ]; then
  DISPLAY_ARGS="-display none"
else
  DISPLAY_ARGS="-device virtio-gpu-device -display cocoa"
fi

# Start QEMU
$QEMU \
  -machine virt \
  -bios default \
  $DISPLAY_ARGS \
  -serial mon:stdio \
  --no-reboot \
    -kernel bin/os.elf
