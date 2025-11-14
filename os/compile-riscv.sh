#!/bin/bash
set -euxo pipefail

# Source common compiler flags and source lists
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build-common.sh"

# Path to clang and compiler flags
CC=/opt/homebrew/opt/llvm/bin/clang  # Ubuntu users: use CC=clang
OBJCOPY=/opt/homebrew/opt/llvm/bin/llvm-objcopy
CPPFLAGS="$COMMON_CPPFLAGS -DOT_ARCH_RISCV -DOT_FEAT_GFX=OT_FEAT_GFX_VIRTIO"
CFLAGS="$COMMON_CFLAGS --target=riscv32-unknown-elf -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib -fno-rtti"

# Add shared file to shared source
COMMON_SHARED_SOURCES+=("ot/shared/shared-riscv.cpp")

$CC $CPPFLAGS $CFLAGS \
    -Wl,-Tot/user/user.ld -Wl,-Map=bin/prog-shell.map -o bin/prog-shell.elf \
    ot/user/user-riscv.cpp \
    "${COMMON_SHARED_SOURCES[@]}" \
    "${USER_SHARED_SOURCES[@]}"

$OBJCOPY --set-section-flags .bss=alloc,contents -O binary bin/prog-shell.elf bin/prog-shell.bin
$OBJCOPY -Ibinary -Oelf32-littleriscv bin/prog-shell.bin bin/prog-shell.bin.o

# Build the kernel
$CC $CPPFLAGS $CFLAGS -Wl,-Tot/kernel/kernel-link.ld -Wl,-Map=bin/kernel.map -o bin/kernel.elf \
    "${KERNEL_SHARED_SOURCES[@]}" \
    "${COMMON_SHARED_SOURCES[@]}" \
    ot/kernel/platform-riscv.cpp \
    bin/prog-shell.bin.o
