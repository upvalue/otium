#!/bin/bash
set -euxo pipefail

QEMU=qemu-system-riscv32

# Build the kernel
./compile-riscv.sh "$@"

# Start QEMU
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -kernel otk/kernel.elf
