// arena.cpp - Arena allocator C API wrappers
#include "ot/lib/arena.hpp"

// C API wrapper implementations for use by C code (like schrift.c)
extern "C" {

void *sft_arena_alloc(void *arena, size_t size) {
  if (!arena)
    return nullptr;
  return static_cast<lib::Arena *>(arena)->alloc(size);
}

void *sft_arena_alloc_zeroed(void *arena, size_t size) {
  if (!arena)
    return nullptr;
  return static_cast<lib::Arena *>(arena)->alloc_zeroed(size);
}

} // extern "C"
