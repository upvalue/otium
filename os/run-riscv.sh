#!/bin/bash
set -euxo pipefail

QEMU=qemu-system-riscv32
DISPLAY=cocoa

# Build the OS
./compile-riscv.sh "$@"

# Start QEMU
$QEMU \
  -machine virt \
  -bios default \
  -device virtio-gpu-device \
  -display $DISPLAY \
  -serial mon:stdio \
  --no-reboot \
    -kernel bin/os.elf
