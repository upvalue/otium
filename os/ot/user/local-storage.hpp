// local-storage.hpp - Per-process local storage for user programs
// This page is managed by the kernel and updated on context switch

#ifndef OT_USER_LOCAL_STORAGE_HPP
#define OT_USER_LOCAL_STORAGE_HPP

#include "ot/common.h"
#include "ot/vendor/tlsf/tlsf.h"

/**
 * Base structure for per-process local storage.
 * User programs can inherit from this to add their own process-specific data.
 */
struct LocalStorage {
  char *memory_begin;
  tlsf_t pool;
  size_t memory_pages_allocated;

  /**
   * Initialize the memory allocator for this process.
   * Allocates the specified number of pages and sets up TLSF pool.
   * This must be called before using ou_malloc/ou_free/ou_realloc.
   *
   * @param pages Number of pages to allocate for the memory pool (default 10)
   */
  void process_storage_init(size_t pages = 10);
};

/**
 * Global pointer to the current process's local storage.
 * This is updated by the kernel on context switch.
 * User programs should not modify this pointer directly.
 */
extern LocalStorage *local_storage;

#endif
