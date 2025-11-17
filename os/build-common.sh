#!/bin/bash
# Common compiler flags and source file lists shared across all architectures

# Flags common to all architectures
COMMON_CFLAGS="-Os -g3 -Wall -Wextra -Wno-unused-parameter -fno-exceptions "
COMMON_CPPFLAGS="-I."

# Core OS source files (platform-independent)
CORE_SOURCES=(
  ot/core/startup.cpp
  ot/core/main.cpp
  ot/core/kernel-tests.cpp
  ot/core/memory.cpp
  ot/core/process.cpp
  ot/core/std.cpp
)

# Driver source files
DRIVER_SOURCES=(
  ot/core/drivers/drv-virtio.cpp
  ot/core/drivers/drv-gfx-virtio.cpp
)

# Library source files (shared utilities)
LIB_SOURCES=(
  ot/lib/std.cpp
  ot/lib/mpack/mpack-reader.cpp
  ot/lib/mpack/mpack-utils.cpp
  ot/vendor/libmpack/mpack.c
  ot/vendor/printf/printf.c
)

# User program source files
USER_SOURCES=(
  ot/user/user-main.cpp
  ot/user/prog-shell.cpp
  ot/user/tcl.cpp
  ot/user/string.cpp
  ot/user/memory-allocator.cpp
  ot/user/comm-writer.cpp
  ot/vendor/tlsf/tlsf.c
)

# Generated IPC code (add service implementations as needed)
# Uncomment after running ./generate-ipc.sh
USER_SOURCES+=(
  ot/user/gen/fibonacci-client.cpp
  ot/user/gen/fibonacci-server.cpp
  ot/user/fibonacci/impl.cpp
)
