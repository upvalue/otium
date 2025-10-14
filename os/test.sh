#!/usr/bin/env bash
set -euxo pipefail
clang++ -DOT_TEST -DOT_TRACE_MEM \
    -I. -o test \
    otk/memory-test.cpp \
    otk/memory.cpp \
    otk/std.cpp \
    otk/platform-test.cpp 
./test