#!/bin/bash
set -xue

cargo build

# QEMU file path
QEMU=qemu-system-riscv32

# Start QEMU
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
  -kernel target/riscv32imac-unknown-none-elf/debug/os
