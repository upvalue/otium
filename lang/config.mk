CC ?= cc
AR ?= ar
PYTHON ?= python3

# Collector implementation. "semi" is the whole-heap copying collector,
# "gen" is the copying-nursery/mark-sweep collector, and "gsgc" is the
# transplanted generation-scavenging collector.
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

# Generation-scavenging policy. Objects move to old space after this many
# minor collections. A large remembered set asks the collector to promote
# young referents with a full collection.
GSGC_MAX_AGE ?= 4
GSGC_MAX_REMEMBERED ?= 1024
GSGC_MIN_NEW_SPACE ?= 1048576

RAYLIB_CFLAGS ?= $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS ?= $(shell pkg-config --libs raylib 2>/dev/null)
WITH_RAY ?= $(if $(strip $(RAYLIB_LIBS)),1,0)
