#!/bin/bash
# Common compiler flags shared across all architectures

# Flags common to all architectures
COMMON_CFLAGS="-O2 -g3 -Wall -Wextra -Wno-unused-parameter -fno-exceptions -fno-rtti"
COMMON_CPPFLAGS="-I. -DOT_TRACE_MEM"
