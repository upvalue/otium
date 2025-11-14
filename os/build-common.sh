#!/bin/bash
# Common compiler flags and source file lists shared across all architectures

# Flags common to all architectures
COMMON_CFLAGS="-Os -g3 -Wall -Wextra -Wno-unused-parameter -fno-exceptions "
COMMON_CPPFLAGS="-I."

# Shared kernel source files (platform-independent)
KERNEL_SHARED_SOURCES=(
  ot/kernel/startup.cpp
  ot/kernel/main.cpp
  ot/kernel/memory.cpp
  ot/kernel/ipc.cpp
  ot/kernel/process.cpp
  ot/kernel/std.cpp
  ot/kernel/drv-virtio.cpp
  ot/kernel/drv-gfx-virtio.cpp
)

COMMON_SHARED_SOURCES=(
  ot/shared/std.cpp
  vendor/libmpack/mpack.c
  ot/shared/mpack-reader.cpp
  ot/shared/mpack-utils.cpp
  ot/shared/vendor/printf.c
)

# Shared user program source files (platform-independent)
USER_SHARED_SOURCES=(
  ot/user/user-main.cpp
  ot/user/prog-shell.cpp
  ot/user/string.cpp
  ot/user/tcl.cpp
  ot/user/vendor/tlsf.c
)
