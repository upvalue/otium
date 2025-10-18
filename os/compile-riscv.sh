#!/bin/bash
set -euxo pipefail

# Parse test program argument
TEST_PROG=""
if [ "${1:-}" = "--test-hello" ]; then
  TEST_PROG="-DKERNEL_PROG_TEST_HELLO"
elif [ "${1:-}" = "--test-mem" ]; then
  TEST_PROG="-DKERNEL_PROG_TEST_MEM"
fi

# Path to clang and compiler flags
CC=/opt/homebrew/opt/llvm/bin/clang  # Ubuntu users: use CC=clang
OBJCOPY=/opt/homebrew/opt/llvm/bin/llvm-objcopy
CPPFLAGS="-I. -DOT_TRACE_MEM $TEST_PROG"
CFLAGS="-O2 -g3 -Wall -Wextra --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib -fno-exceptions -fno-rtti"

$CC $CPPFLAGS $CFLAGS -Wl,-Totu/user.ld -Wl,-Map=otu/prog-shell.map -o otu/prog-shell.elf otu/user-riscv.cpp otu/prog-shell.cpp
$OBJCOPY --set-section-flags .bss=alloc,contents -O binary otu/prog-shell.elf otu/prog-shell.bin
$OBJCOPY -Ibinary -Oelf32-littleriscv otu/prog-shell.bin otu/prog-shell.bin.o

# Build the kernel
$CC $CPPFLAGS $CFLAGS -Wl,-Totk/kernel-link.ld -Wl,-Map=otk/kernel.map -o otk/kernel.elf \
    otk/kernel.cpp \
    otk/platform-riscv.cpp \
    otk/std.cpp \
    otk/memory.cpp \
    otk/process.cpp \
    otu/prog-shell.bin.o
