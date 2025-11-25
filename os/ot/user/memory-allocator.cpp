// memory-allocator.cpp - Memory allocation functions for user programs
// Uses TLSF allocator with per-process local storage

#include "ot/user/local-storage.hpp"
#include "ot/user/user.hpp"
#include "ot/vendor/tlsf/tlsf.h"

// Global pointer to current process's local storage (updated by kernel on context switch)
LocalStorage *local_storage = nullptr;

void LocalStorage::process_storage_init(size_t pages) {
  // Allocate contiguous pages for the memory pool
  memory_begin = (char *)ou_alloc_page();
  if (!memory_begin) {
    oprintf("FATAL: process_storage_init failed to allocate first page\n");
    ou_exit();
  }

  pool = tlsf_create_with_pool(memory_begin, OT_PAGE_SIZE);

  if (!pool) {
    oprintf("FATAL: failed to create TLSF memory pool\n");
    ou_exit();
  }

  // Allocate additional pages to get a contiguous region
  for (size_t i = 1; i < pages; i++) {
    void *page = ou_alloc_page();
    if (!page) {
      oprintf("FATAL: process_storage_init failed to allocate page %d of %d\n", i, pages);
      ou_exit();
    }

    if (!tlsf_add_pool(pool, page, OT_PAGE_SIZE)) {
      oprintf("FATAL: failed to add page %d to TLSF memory pool\n", i);
      ou_exit();
    }
  }

  memory_pages_allocated = pages;

  // Create TLSF pool from the allocated memory
  pool = tlsf_create_with_pool(memory_begin, pages * OT_PAGE_SIZE);
  if (!pool) {
    oprintf("FATAL: failed to create TLSF memory pool\n");
    ou_exit();
  }
}

void *ou_malloc(size_t size) {
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

void ou_free(void *ptr) {
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

void *ou_realloc(void *ptr, size_t size) {
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
