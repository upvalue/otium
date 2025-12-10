// arena.hpp - Simple bump-pointer arena allocator
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h> // for memset

#include "ot/user/user.hpp"

// Set to 1 to bypass arena and use ou_malloc directly (for debugging)
#define ARENA_USE_MALLOC_PASSTHROUGH 0

// Set to 1 to enable bounds checking and logging
#define ARENA_DEBUG 0

namespace lib {

// Simple bump-pointer arena allocator
// - Accepts raw memory (typically OS pages)
// - No TLSF or external allocator dependency
// - Supports reset for reuse
class Arena {
public:
  // Initialize with memory region
  Arena(void *memory, size_t size) : base_(static_cast<uint8_t *>(memory)), size_(size), pos_(0) {
#if ARENA_DEBUG
    oprintf("[arena] init: base=%p size=%u end=%p\n", base_, size_, base_ + size_);
#endif
  }

  // Allocate aligned memory (returns nullptr if exhausted)
  void *alloc(size_t size, size_t align = 8) {
#if ARENA_USE_MALLOC_PASSTHROUGH
    (void)align;
    return ou_malloc(size);
#else
    // Align position up
    size_t aligned_pos = (pos_ + align - 1) & ~(align - 1);
    size_t new_pos = aligned_pos + size;

    if (new_pos <= size_) {
      void *result = base_ + aligned_pos;
#if ARENA_DEBUG
      oprintf("[arena] alloc(%u) -> %p (pos %u -> %u, remaining %u)\n", size, result, pos_, new_pos, size_ - new_pos);
      // Bounds check
      if (result < (void *)base_ || (uint8_t *)result + size > base_ + size_) {
        oprintf("[arena] ERROR: allocation %p-%p outside bounds %p-%p!\n", result, (uint8_t *)result + size, base_,
                base_ + size_);
      }
#endif
      pos_ = new_pos;
      return result;
    }

#if ARENA_DEBUG
    oprintf("[arena] alloc(%u) FAILED - need %u but only %u remaining\n", size, new_pos, size_ - pos_);
#endif
    return nullptr;
#endif
  }

  // Allocate zeroed memory (like calloc)
  void *alloc_zeroed(size_t size, size_t align = 8) {
    void *ptr = alloc(size, align);
    if (ptr) {
      memset(ptr, 0, size);
    }
    return ptr;
  }

  // Reset arena to empty (all previous allocations invalidated)
  void reset() {
#if !ARENA_USE_MALLOC_PASSTHROUGH
    pos_ = 0;
#endif
    // In passthrough mode, we can't free - caller must track allocations
  }

  // Query usage
  size_t used() const { return pos_; }
  size_t capacity() const { return size_; }
  size_t remaining() const { return size_ - pos_; }

private:
  uint8_t *base_;
  size_t size_;
  size_t pos_;
};

} // namespace lib
