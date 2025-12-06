// memory-allocator.cpp - Memory allocation functions for user programs
// Uses TLSF allocator with per-process local storage

#include "ot/user/local-storage.hpp"
#include "ot/user/user.hpp"
#include "ot/vendor/tlsf/tlsf.h"

// Global pointer to current process's local storage (updated by kernel on context switch)
LocalStorage *local_storage = nullptr;

void LocalStorage::process_storage_init(size_t pages) {
  if (pages == 0) {
    return;
  }

  // Allocate contiguous pages for the memory pool
  // This allows TLSF to allocate blocks larger than a single page
  memory_begin = (char *)ou_alloc_pages(pages);
  if (!memory_begin) {
    oprintf("FATAL: process_storage_init failed to allocate %d contiguous pages\n", pages);
    ou_exit();
  }

  // Create TLSF pool from the contiguous memory region
  pool = tlsf_create_with_pool(memory_begin, pages * OT_PAGE_SIZE);
  if (!pool) {
    oprintf("FATAL: failed to create TLSF memory pool\n");
    ou_exit();
  }

  memory_pages_allocated = pages;
}

extern "C" void *ou_malloc(size_t size) {
  if (!local_storage) {
    oprintf("FATAL: ou_malloc called before local_storage initialized\n");
    ou_exit();
  }
  if (!local_storage->pool) {
    oprintf("FATAL: ou_malloc called before pool initialized (size=%d)\n", size);
    oprintf("       Did you forget to call process_storage_init()?\n");
    ou_exit();
  }
  void *result = tlsf_malloc(local_storage->pool, size);
  if (!result && size > 0) {
    oprintf("FATAL: ou_malloc failed - out of memory (requested=%d)\n", size);
    ou_exit();
  }
  return result;
}

extern "C" void ou_free(void *ptr) {
  if (!local_storage) {
    oprintf("WARNING: ou_free called before local_storage initialized\n");
    return;
  }
  if (!local_storage->pool) {
    oprintf("WARNING: ou_free called before pool initialized\n");
    return;
  }
  tlsf_free(local_storage->pool, ptr);
}

extern "C" void *ou_realloc(void *ptr, size_t size) {
  if (!local_storage) {
    oprintf("FATAL: ou_realloc called before local_storage initialized\n");
    ou_exit();
  }
  if (!local_storage->pool) {
    oprintf("FATAL: ou_realloc called before pool initialized\n");
    ou_exit();
  }
  void *result = tlsf_realloc(local_storage->pool, ptr, size);
  if (!result && size > 0) {
    oprintf("FATAL: ou_realloc failed - out of memory (requested=%d)\n", size);
    ou_exit();
  }
  return result;
}
