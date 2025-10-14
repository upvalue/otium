#!/bin/bash
set -euxo pipefail

QEMU=qemu-system-riscv32

# Path to clang and compiler flags
CC=/opt/homebrew/opt/llvm/bin/clang  # Ubuntu users: use CC=clang
CPPFLAGS="-I."
CFLAGS="-O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib"

# Build the kernel
$CC $CPPFLAGS $CFLAGS -Wl,-Totk/kernel-link.ld -Wl,-Map=otk/kernel.map -o otk/kernel.elf \
    -fno-exceptions -fno-rtti \
    otk/kernel.cpp otk/platform-riscv.cpp otk/std.cpp otk/memory.cpp

# Start QEMU
$QEMU -machine virt -bios default -nographic -serial mon:stdio --no-reboot \
    -kernel otk/kernel.elf
