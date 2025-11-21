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


# Library source files (shared utilities)
LIB_SOURCES=(
  ot/lib/std.cpp
  ot/lib/gfx-util.cpp
  ot/lib/mpack/mpack-reader.cpp
  ot/lib/mpack/mpack-utils.cpp
  ot/vendor/libmpack/mpack.c
  ot/vendor/printf/printf.c
)

# User program source files
USER_SOURCES=(
  ot/user/user-main.cpp
  ot/user/prog-scratch.cpp
  ot/user/prog-shell.cpp
  ot/user/prog-spacedemo.cpp
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
  ot/user/gen/graphics-client.cpp
  ot/user/gen/graphics-server.cpp
  ot/user/graphics/impl.cpp
  ot/user/gen/filesystem-client.cpp
  ot/user/gen/filesystem-server.cpp
  ot/lib/frame-manager.cpp
)

# Add filesystem implementation based on config
if grep -q "^#define OT_FILESYSTEM_BACKEND OT_FILESYSTEM_BACKEND_MEMORY" ot/config.h 2>/dev/null; then
  USER_SOURCES+=(ot/user/filesystem/impl-memory.cpp)
elif grep -q "^#define OT_FILESYSTEM_BACKEND OT_FILESYSTEM_BACKEND_NONE" ot/config.h 2>/dev/null; then
  USER_SOURCES+=(ot/user/filesystem/impl-none.cpp)
else
  # Default to memory if config doesn't exist yet
  USER_SOURCES+=(ot/user/filesystem/impl-memory.cpp)
fi
