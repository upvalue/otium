CC ?= cc
AR ?= ar
PYTHON ?= python3

# Build outputs belong to this checkout and platform. Override BUILD_ROOT or
# BIN_ROOT for external build storage.
PLATFORM ?= $(shell uname -m)-$(shell uname -s | tr '[:upper:]' '[:lower:]')
BUILD_ROOT ?= build
BIN_ROOT ?= bin

# Collector implementation. "semi" is the whole-heap copying collector and
# "gen" is the copying-nursery/mark-sweep collector.
GC ?= semi

CSTD ?= -std=c23
OPT ?= -O2
WARN ?= -Wall -Wextra -Wpedantic -Werror -Wshadow
CFLAGS ?= $(CSTD) $(OPT) -g $(WARN)
CPPFLAGS ?=
EXTRA_CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?= -lm

HEAP_INIT ?= 1048576
HEAP_MAX ?= 67108864
MAX_DEPTH ?= 200000

# Host-oriented generational collector geometry.  These are deliberately
# build-time knobs so constrained targets can provide a different profile
# later without spreading platform checks through the collector.
GC_NURSERY_BYTES ?= 2097152
GC_OLD_CHUNK_BYTES ?= 1048576
GC_LARGE_OBJECT_BYTES ?= 262144
GC_MARK_STACK_ENTRIES ?= 16384
GC_TIMING ?= 1
