#!/usr/bin/env bash
set -euxo pipefail
mkdir -p bin

# Compile mpack as C
clang -DOT_TEST -I. -c vendor/libmpack/mpack.c -o bin/mpack.o

# Compile and link tests
clang++ -DOT_TEST -DOT_TRACE_MEM \
    -I. -o bin/test \
    ot/kernel/memory-test.cpp \
    ot/kernel/memory.cpp \
    ot/shared/std.cpp \
    ot/kernel/platform-test.cpp \
    ot/kernel/process-test.cpp \
    ot/shared/regstr-test.cpp \
    ot/shared/mpacker-test.cpp \
    bin/mpack.o

./bin/test
