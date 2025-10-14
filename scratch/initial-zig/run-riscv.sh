#!/bin/bash
set -xue

# QEMU file path
QEMU=qemu-system-riscv32

# Build the kernel using Zig
zig build -Dprogram=echo

# Start QEMU
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot -kernel zig-out/bin/kernel.elf
