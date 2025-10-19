#!/usr/bin/env bash
set -euxo pipefail
mkdir -p bin
clang++ -DOT_TEST -DOT_TRACE_MEM \
    -I. -o bin/test \
    ot/kernel/memory-test.cpp \
    ot/kernel/memory.cpp \
    ot/shared/std.cpp \
    ot/kernel/platform-test.cpp \
    ot/kernel/process-test.cpp \
    ot/shared/regstr-test.cpp
./bin/test
