#!/bin/bash

# Common compiler flags
COMMON_CPPFLAGS="-D__STDC_HOSTED__=0 -std=c++20"
COMMON_CFLAGS="-Os -g -Wall -Wextra -fno-exceptions -fno-rtti"

# Source file lists
KERNEL_SHARED_SOURCES=(
    "ot/kernel/main.cpp"
)

USER_SHARED_SOURCES=(
    "ot/user/tevl.cpp"
)

COMMON_SHARED_SOURCES=(
    # Will be populated by architecture-specific build script
)