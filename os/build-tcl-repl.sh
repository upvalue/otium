#!/usr/bin/env bash
set -euxo pipefail
mkdir -p bin

# Compile TCL interpreter and REPL with POSIX flag
clang++ -DOT_POSIX -std=c++17 -I. -o bin/tcl-repl \
    ot/user/tcl.cpp \
    ot/posix-util/tcl-repl.cpp \
    ot/posix-util/posix-std.cpp \
    ot/posix-util/vendor/bestline.c \
    ot/shared/std.cpp

echo "✓ TCL REPL built successfully: bin/tcl-repl"
