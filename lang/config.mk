CC ?= cc
AR ?= ar
PYTHON ?= python3

# Collector implementation.  "semi" is the existing whole-heap copying
# collector; "gen" is the copying-nursery/mark-sweep collector.
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

RAYLIB_CFLAGS ?= $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS ?= $(shell pkg-config --libs raylib 2>/dev/null)
WITH_RAY ?= $(if $(strip $(RAYLIB_LIBS)),1,0)
