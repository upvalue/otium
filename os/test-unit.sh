#!/usr/bin/env bash
set -euxo pipefail
mkdir -p bin

# Compile C libraries
clang -DOT_POSIX -DOT_TEST -I. -c ot/vendor/libmpack/mpack.c -o bin/mpack.o
clang -DOT_POSIX -DOT_TEST -I. -c ot/vendor/printf/printf.c -o bin/printf.o

# Compile and link tests
clang++ -DOT_POSIX -DOT_TEST -DOT_TRACE_MEM -DBUILDING_KERNEL \
    -I. -o bin/test \
    ot/test-linking.cpp \
    ot/core/memory-test.cpp \
    ot/core/memory.cpp \
    ot/core/std.cpp \
    ot/lib/std.cpp \
    ot/core/platform/posix-std.cpp \
    ot/lib/printf-test.cpp \
    ot/core/platform-test.cpp \
    ot/core/process.cpp \
    ot/core/process-test.cpp \
    ot/lib/mpack/mpack-writer-test.cpp \
    ot/lib/mpack/mpack-reader.cpp \
    ot/lib/mpack/mpack-reader-test.cpp \
    ot/lib/mpack/mpack-utils.cpp \
    ot/lib/mpack/mpack-utils-test.cpp \
    ot/lib/result-test.cpp \
    ot/lib/string.cpp \
    ot/lib/string-test.cpp \
    ot/lib/vector-test.cpp \
    ot/user/tcl.cpp \
    ot/user/tcl-test.cpp \
    bin/mpack.o \
    bin/printf.o

./bin/test
