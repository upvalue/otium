#!/usr/bin/env bash
set -euxo pipefail
mkdir -p bin

# Compile C files separately
clang -DOT_POSIX -std=c11 -I. -c ot/posix-util/vendor/bestline.c -o bin/bestline.o
clang -DOT_POSIX -std=c11 -I. -c vendor/libmpack/mpack.c -o bin/mpack.o
clang -DOT_POSIX -std=c11 -I. -c ot/shared/vendor/printf.c -o bin/printf.o

# Build TCL REPL
clang++ -DOT_POSIX -std=c++17 -I. -o bin/tcl-repl \
    ot/user/string.cpp \
    ot/user/tcl.cpp \
    ot/posix-util/tcl-repl.cpp \
    ot/posix-util/posix-std.cpp \
    ot/shared/std.cpp \
    ot/shared/mpack-utils.cpp \
    bin/bestline.o \
    bin/mpack.o \
    bin/printf.o

echo "✓ TCL REPL built successfully: bin/tcl-repl"

# Build TEVL text editor
clang++ -DOT_POSIX -std=c++17 -I. -o bin/tevl \
    ot/user/string.cpp \
    ot/user/file.cpp \
    ot/user/tevl.cpp \
    ot/posix-util/posix-file.cpp \
    ot/posix-util/tevl-posix.cpp \
    ot/posix-util/posix-std.cpp \
    ot/shared/std.cpp \
    bin/printf.o

echo "✓ TEVL built successfully: bin/tevl"
