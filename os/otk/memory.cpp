// memory.cpp - page manager

#include "otk/kernel.hpp"

static uintptr_t next_page_addr = (uintptr_t)__free_ram;

void *page_allocate_impl(char *begin, char *end, char **next,
                         size_t page_count) {
  uintptr_t page_addr = (uintptr_t)*next;
  (*next) += page_count * PAGE_SIZE;

  if ((uintptr_t)*next > (uintptr_t)end) {
    PANIC("out of memory");
  }

  // Trace page number based on pointer arithmetic,
  TRACE_MEM("page_allocate_impl: allocated page %d at address %x after request "
            "to allocate %d pages",
            (page_addr - (uintptr_t)begin) / PAGE_SIZE, page_addr, page_count);

  omemset((void *)page_addr, 0, page_count * PAGE_SIZE);
  return (void *)page_addr;
}

void *page_allocate(size_t page_count) {
  return page_allocate_impl((char *)__free_ram, (char *)__free_ram_end,
                            (char **)&next_page_addr, page_count);
}