#!/bin/bash
set -euxo pipefail

# Source common compiler flags
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common-flags.sh"

# Path to clang and compiler flags
CC=/opt/homebrew/opt/llvm/bin/clang  # Ubuntu users: use CC=clang
OBJCOPY=/opt/homebrew/opt/llvm/bin/llvm-objcopy
CPPFLAGS="$COMMON_CPPFLAGS -DOT_ARCH_RISCV"
CFLAGS="$COMMON_CFLAGS --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib"

$CC $CPPFLAGS $CFLAGS \
    -Wl,-Totu/user.ld -Wl,-Map=otu/prog-shell.map -o otu/prog-shell.elf \
    otu/user-riscv.cpp \
    otu/prog-shell.cpp \
    otk/std.cpp \
    otu/vendor/tlsf.c \
    otu/tcl.cpp
    
$OBJCOPY --set-section-flags .bss=alloc,contents -O binary otu/prog-shell.elf otu/prog-shell.bin
$OBJCOPY -Ibinary -Oelf32-littleriscv otu/prog-shell.bin otu/prog-shell.bin.o

# Build the kernel
$CC $CPPFLAGS $CFLAGS -Wl,-Totk/kernel-link.ld -Wl,-Map=otk/kernel.map -o otk/kernel.elf \
    otk/kernel.cpp \
    otk/kernel-prog.cpp \
    otk/platform-riscv.cpp \
    otk/std.cpp \
    otk/memory.cpp \
    otk/process.cpp \
    otu/prog-shell.bin.o
