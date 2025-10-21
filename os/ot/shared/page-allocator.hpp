#ifndef OT_SHARED_PAGE_ALLOCATOR_HPP
#define OT_SHARED_PAGE_ALLOCATOR_HPP

#include "ot/common.h"
#include "ot/shared/address.hpp"
#include "ot/shared/pair.hpp"

/**
 * PageAllocator - A simple bump allocator for allocating within a single page
 *
 * This utility allows allocating chunks of memory from a single page,
 * tracking how much has been allocated and preventing overflow.
 *
 * Handles both physical and virtual addresses for MMU-based systems.
 */
struct PageAllocator {
  PageAddr page_paddr; // The physical address of the page
  PageAddr page_vaddr; // The virtual address of the page
  size_t allocated;    // How much has been allocated from this page (in bytes)

  /**
   * Initialize the allocator with physical and virtual page addresses
   */
  PageAllocator(PageAddr paddr, PageAddr vaddr)
      : page_paddr(paddr), page_vaddr(vaddr), allocated(0) {}

  /**
   * Allocate 'size' bytes from the page (defaults to sizeof(T))
   * Returns Pair<physical_ptr, virtual_ptr> or Pair<nullptr, nullptr> on failure
   * Template parameter T provides type safety for allocations.
   *
   * Physical pointer should be used for writing (kernel can access)
   * Virtual pointer should be stored in data structures (user will access)
   */
  template <typename T> Pair<T *, T *> alloc(size_t size = sizeof(T)) {
    // Check if we have enough space
    if (allocated + size > OT_PAGE_SIZE) {
      return make_pair((T *)nullptr, (T *)nullptr);
    }

    // Calculate the addresses for this allocation
    T *pptr = (T *)((uintptr_t)page_paddr.as_ptr() + allocated);
    T *vptr = (T *)((uintptr_t)page_vaddr.as_ptr() + allocated);

    // Update allocated count
    allocated += size;

    return make_pair(pptr, vptr);
  }

  /**
   * Get the remaining space in the page
   */
  size_t remaining() const { return OT_PAGE_SIZE - allocated; }

  /**
   * Reset the allocator (does not clear memory, just resets the counter)
   */
  void reset() { allocated = 0; }
};

#endif
