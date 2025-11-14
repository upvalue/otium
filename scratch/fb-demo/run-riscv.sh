#!/bin/bash
set -euxo pipefail

QEMU=qemu-system-riscv32
DISPLAY=cocoa 

# Build the kernel
./compile-riscv.sh "$@"

# Start QEMU with virtio-gpu device (no BIOS - direct execution)
$QEMU \
  -machine virt \
  -bios none \
  -device virtio-gpu-device \
  -display $DISPLAY \
  -serial mon:stdio \
  --no-reboot \
  -kernel bin/kernel.elf
