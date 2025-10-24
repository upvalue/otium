#!/usr/bin/env bash
set -euxo pipefail
mkdir -p bin

# Compile C libraries
clang -DOT_POSIX -DOT_TEST -I. -c vendor/libmpack/mpack.c -o bin/mpack.o
clang -DOT_POSIX -DOT_TEST -I. -c ot/shared/vendor/printf.c -o bin/printf.o

# Compile and link tests
clang++ -DOT_POSIX -DOT_TEST -DOT_TRACE_MEM \
    -I. -o bin/test \
    ot/test-linking.cpp \
    ot/kernel/memory-test.cpp \
    ot/kernel/memory.cpp \
    ot/kernel/std.cpp \
    ot/shared/std.cpp \
    ot/posix-util/posix-std.cpp \
    ot/shared/printf-test.cpp \
    ot/kernel/platform-test.cpp \
    ot/kernel/process.cpp \
    ot/kernel/process-test.cpp \
    ot/shared/mpack-writer-test.cpp \
    ot/shared/mpack-reader.cpp \
    ot/shared/mpack-reader-test.cpp \
    ot/shared/mpack-utils.cpp \
    ot/shared/mpack-utils-test.cpp \
    ot/shared/result-test.cpp \
    ot/user/tcl.cpp \
    ot/user/tcl-test.cpp \
    bin/mpack.o \
    bin/printf.o

./bin/test
