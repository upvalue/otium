#!/bin/bash
set -euxo pipefail

QEMU=qemu-system-riscv32
DISPLAY=cocoa 

# Build the kernel
./compile-riscv.sh "$@"

# Start QEMU
$QEMU \
  -machine virt \
  -bios default \
  -device ramfb \
  -display $DISPLAY \
  -serial mon:stdio \
  --no-reboot \
    -kernel bin/kernel.elf
