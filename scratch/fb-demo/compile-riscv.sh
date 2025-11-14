#!/bin/bash
set -euxo pipefail

# Create bin directory if it doesn't exist
mkdir -p bin

# Path to clang and compiler flags
CC=/opt/homebrew/opt/llvm/bin/clang  # Ubuntu users: use CC=clang

# Compiler flags for bare metal RISC-V
CFLAGS="-Os -g -Wall -Wextra --target=riscv32-unknown-elf -march=rv32imac -mabi=ilp32"
CFLAGS="$CFLAGS -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib"
CFLAGS="$CFLAGS -fno-exceptions -fno-rtti -std=c++17"

# Build the kernel with graphics demo
$CC $CFLAGS -Wl,-Tlink.ld -Wl,-Map=bin/kernel.map -o bin/kernel.elf gfx-demo.cpp
