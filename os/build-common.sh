#!/bin/bash
# Common compiler flags and source file lists shared across all architectures

# Flags common to all architectures
COMMON_CFLAGS="-Os -g3 -Wall -Wextra -Wno-unused-parameter -fno-exceptions "
COMMON_CPPFLAGS="-I."

# Core OS source files (platform-independent)
CORE_SOURCES=(
  ot/core/startup.cpp
  ot/core/main.cpp
  ot/core/memory.cpp
  ot/core/ipc.cpp
  ot/core/process.cpp
  ot/core/std.cpp
)

# Driver source files
DRIVER_SOURCES=(
  ot/drivers/drv-virtio.cpp
  ot/drivers/drv-gfx-virtio.cpp
)

# Library source files (shared utilities)
LIB_SOURCES=(
  ot/lib/std.cpp
  ot/lib/mpack/mpack-reader.cpp
  ot/lib/mpack/mpack-utils.cpp
  ot/vendor/libmpack/mpack.c
  ot/vendor/printf/printf.c
)

# Program source files
PROGRAM_SOURCES=(
  ot/platform/user-main.cpp
  ot/programs/prog-shell.cpp
  ot/programs/tcl.cpp
  ot/lib/string.cpp
  ot/vendor/tlsf/tlsf.c
)
