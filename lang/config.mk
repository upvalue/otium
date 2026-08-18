CC ?= cc
AR ?= ar
PYTHON ?= python3

CSTD ?= -std=c23
OPT ?= -O2
WARN ?= -Wall -Wextra -Wpedantic -Werror -Wshadow
CFLAGS ?= $(CSTD) $(OPT) -g $(WARN)
CPPFLAGS ?=
LDFLAGS ?=
LDLIBS ?= -lm

HEAP_INIT ?= 1048576
HEAP_MAX ?= 67108864
MAX_DEPTH ?= 200000

RAYLIB_CFLAGS ?= $(shell pkg-config --cflags raylib 2>/dev/null)
RAYLIB_LIBS ?= $(shell pkg-config --libs raylib 2>/dev/null)
WITH_RAY ?= $(if $(strip $(RAYLIB_LIBS)),1,0)
