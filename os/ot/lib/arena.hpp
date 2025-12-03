// arena.hpp - Simple bump-pointer arena allocator
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h> // for memset

namespace lib {

// Simple bump-pointer arena allocator
// - Accepts raw memory (typically OS pages)
// - No TLSF or external allocator dependency
// - Supports reset for reuse
// - Optional fallback to external allocator for oversized requests
class Arena {
public:
  // Initialize with memory region
  Arena(void *memory, size_t size)
      : base_(static_cast<uint8_t *>(memory)), size_(size), pos_(0),
        fallback_alloc_(nullptr), fallback_free_(nullptr), fallback_count_(0) {
    fallback_ptrs_ = fallback_storage_;
  }

  // Allocate aligned memory (returns nullptr if exhausted and no fallback)
  void *alloc(size_t size, size_t align = 8) {
    // Align position up
    size_t aligned_pos = (pos_ + align - 1) & ~(align - 1);
    size_t new_pos = aligned_pos + size;

    if (new_pos <= size_) {
      pos_ = new_pos;
      return base_ + aligned_pos;
    }

    // Fallback for oversized or exhausted arena
    if (fallback_alloc_ && fallback_count_ < MAX_FALLBACKS) {
      void *ptr = fallback_alloc_(size);
      if (ptr) {
        fallback_ptrs_[fallback_count_++] = ptr;
      }
      return ptr;
    }

    return nullptr;
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
  void reset() { pos_ = 0; }

  // Query usage
  size_t used() const { return pos_; }
  size_t capacity() const { return size_; }
  size_t remaining() const { return size_ - pos_; }

  // Set fallback allocator for oversized requests (optional)
  using FallbackAlloc = void *(*)(size_t);
  using FallbackFree = void (*)(void *);

  void set_fallback(FallbackAlloc alloc_fn, FallbackFree free_fn) {
    fallback_alloc_ = alloc_fn;
    fallback_free_ = free_fn;
  }

  // Free fallback allocations (call before reset if fallback was used)
  void free_fallbacks() {
    if (fallback_free_) {
      for (size_t i = 0; i < fallback_count_; i++) {
        fallback_free_(fallback_ptrs_[i]);
      }
    }
    fallback_count_ = 0;
  }

private:
  uint8_t *base_;
  size_t size_;
  size_t pos_;

  // Track fallback allocations for cleanup
  FallbackAlloc fallback_alloc_;
  FallbackFree fallback_free_;
  void **fallback_ptrs_;
  size_t fallback_count_;
  static constexpr size_t MAX_FALLBACKS = 8;
  void *fallback_storage_[MAX_FALLBACKS];
};

} // namespace lib
