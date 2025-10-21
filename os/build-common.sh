#!/bin/bash
# Common compiler flags and source file lists shared across all architectures

# Flags common to all architectures
COMMON_CFLAGS="-O2 -g3 -Wall -Wextra -Wno-unused-parameter -fno-exceptions -fno-rtti"
COMMON_CPPFLAGS="-I."

# Shared kernel source files (platform-independent)
KERNEL_SHARED_SOURCES=(
  ot/kernel/startup.cpp
  ot/kernel/main.cpp
  ot/kernel/memory.cpp
  ot/kernel/process.cpp
)

COMMON_SHARED_SOURCES=(
  ot/shared/std.cpp
  vendor/libmpack/mpack.c
)

# Shared user program source files (platform-independent)
USER_SHARED_SOURCES=(
  ot/user/prog-shell.cpp
  ot/user/user-shared.cpp
  ot/user/tcl.cpp
  ot/user/vendor/tlsf.c
)
