#!/usr/bin/env bash
set -euxo pipefail
mkdir -p bin

# Compile C files separately
clang -DOT_POSIX -std=c11 -I. -c ot/vendor/bestline/bestline.c -o bin/bestline.o
clang -DOT_POSIX -std=c11 -I. -c ot/vendor/libmpack/mpack.c -o bin/mpack.o
clang -DOT_POSIX -std=c11 -I. -c ot/vendor/printf/printf.c -o bin/printf.o

# Build TCL REPL
clang++ -DOT_POSIX -std=c++17 -I. -o bin/tcl-repl \
    ot/lib/string.cpp \
    ot/user/tcl.cpp \
    ot/core/platform/tcl-repl.cpp \
    ot/core/platform/posix-std.cpp \
    ot/lib/std.cpp \
    ot/lib/mpack/mpack-utils.cpp \
    bin/bestline.o \
    bin/mpack.o \
    bin/printf.o

echo "✓ TCL REPL built successfully: bin/tcl-repl"

# Build TEVL text editor
clang++ -DOT_POSIX -std=c++17 -I. -o bin/tevl \
    ot/lib/string.cpp \
    ot/lib/file.cpp \
    ot/user/tevl.cpp \
    ot/user/tcl.cpp \
    ot/lib/mpack/mpack-utils.cpp \
    ot/core/platform/posix-file.cpp \
    ot/core/platform/tevl-posix.cpp \
    ot/core/platform/posix-std.cpp \
    ot/lib/std.cpp \
    bin/printf.o \
    bin/mpack.o

echo "✓ TEVL built successfully: bin/tevl"
