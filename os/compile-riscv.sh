#!/bin/bash
set -euxo pipefail

# Source common compiler flags and source lists
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/build-common.sh"

# Path to clang and compiler flags
CC=/opt/homebrew/opt/llvm/bin/clang  # Ubuntu users: use CC=clang
CPPFLAGS="$COMMON_CPPFLAGS -DOT_ARCH_RISCV -DOT_FEAT_GFX=OT_FEAT_GFX_VIRTIO"
CFLAGS="$COMMON_CFLAGS --target=riscv32-unknown-elf -march=rv32imf -mabi=ilp32f -fuse-ld=lld -fno-stack-protector -ffreestanding -nostdlib -fno-rtti"

# Graphics backend sources (conditional on config.h setting)
# For RISC-V, we include VirtIO backend
GRAPHICS_SOURCES=(
  ot/user/graphics/backend-virtio.cpp
)

# Build the complete OS (kernel + user programs in single executable)
mkdir -p bin
$CC $CPPFLAGS $CFLAGS \
    -Wl,-Tkernel-link-riscv.ld -Wl,-Map=bin/os.map -o bin/os.elf \
    "${CORE_SOURCES[@]}" \
    "${DRIVER_SOURCES[@]}" \
    "${LIB_SOURCES[@]}" \
    "${USER_SOURCES[@]}" \
    "${GRAPHICS_SOURCES[@]}" \
    ot/core/platform/platform-riscv.cpp \
    ot/lib/platform/shared-riscv.cpp \
    ot/core/platform/user-riscv.cpp
