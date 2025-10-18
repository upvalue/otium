// memory.cpp - page manager

#include "otk/kernel.hpp"

static uintptr_t next_page_addr = (uintptr_t)__free_ram;

// Page tracking for recycling
PageInfo *page_infos = nullptr;
static PageInfo *free_list_head = nullptr;
static MemoryStats mem_stats = {0};
static bool memory_initialized = false;
uint32_t total_page_count = 0;

// Bootstrap allocator - only used during memory_init to allocate PageInfo array
static void *page_allocate_bootstrap(size_t page_count) {
  uintptr_t page_addr = next_page_addr;
  next_page_addr += page_count * PAGE_SIZE;

  if (next_page_addr > (uintptr_t)__free_ram_end) {
    PANIC("out of memory during bootstrap");
  }

  TRACE_MEM("Bootstrap allocated %d pages at address %x", page_count, page_addr);

  omemset((void *)page_addr, 0, page_count * PAGE_SIZE);
  return (void *)page_addr;
}

void memory_init() {
  if (memory_initialized) {
    return;
  }

  TRACE("Initializing memory management system");

  // Calculate total number of pages available
  uintptr_t free_ram_size = (uintptr_t)__free_ram_end - (uintptr_t)__free_ram;
  total_page_count = free_ram_size / PAGE_SIZE;

  TRACE("Total pages available: %d", total_page_count);

  // Allocate PageInfo array using bootstrap allocator (before tracking starts)
  size_t page_infos_size = total_page_count * sizeof(PageInfo);
  size_t page_infos_pages = (page_infos_size + PAGE_SIZE - 1) / PAGE_SIZE;
  page_infos = (PageInfo *)page_allocate_bootstrap(page_infos_pages);

  TRACE("Allocated %d pages for PageInfo array at %x", page_infos_pages,
        page_infos);

  // Initialize page tracking structures
  uintptr_t current_page_addr = next_page_addr;
  PageInfo *prev = nullptr;

  for (uint32_t i = 0; i < total_page_count; i++) {
    uintptr_t page_addr = (uintptr_t)__free_ram + i * PAGE_SIZE;

    // Skip pages already allocated for PageInfo array
    if (page_addr < current_page_addr) {
      page_infos[i].pid = 0xFFFFFFFF; // Mark as kernel/system page
      page_infos[i].addr = page_addr;
      page_infos[i].next = nullptr;
      continue;
    }

    // Initialize as free page
    page_infos[i].pid = 0;
    page_infos[i].addr = page_addr;
    page_infos[i].next = nullptr;

    // Link into free list
    if (prev) {
      prev->next = &page_infos[i];
    } else {
      free_list_head = &page_infos[i];
    }
    prev = &page_infos[i];
  }

  // Initialize statistics
  mem_stats.total_pages = total_page_count;
  mem_stats.allocated_pages = page_infos_pages;
  mem_stats.freed_pages = 0;
  mem_stats.processes_created = 0;
  mem_stats.peak_usage_pages = page_infos_pages;

  memory_initialized = true;
  TRACE("Memory initialization complete. Free list head: %x", free_list_head);
}

void *page_allocate(uint32_t pid, size_t page_count) {
  if (!memory_initialized) {
    PANIC("page_allocate called before memory_init");
  }

  TRACE_MEM("page_allocate: pid=%d, count=%d", pid, page_count);

  // For now, only support single page allocation
  if (page_count != 1) {
    PANIC("Multi-page allocation not yet supported in tracked allocator");
  }

  // Take page from free list
  if (!free_list_head) {
    PANIC("Out of memory - no free pages available");
  }

  PageInfo *page_info = free_list_head;
  free_list_head = page_info->next;

  // Mark page as allocated to this process
  page_info->pid = pid;
  page_info->next = nullptr;

  // Update statistics
  mem_stats.allocated_pages++;
  if (mem_stats.allocated_pages > mem_stats.peak_usage_pages) {
    mem_stats.peak_usage_pages = mem_stats.allocated_pages;
  }

  void *page_addr = (void *)page_info->addr;
  omemset(page_addr, 0, PAGE_SIZE);

  TRACE_MEM("Allocated page at %x to pid %d", page_addr, pid);

  return page_addr;
}

void page_free_process(uint32_t pid) {
  if (!memory_initialized) {
    TRACE_MEM("Memory not initialized, cannot free pages");
    return;
  }

  TRACE_MEM("page_free_process: pid=%d", pid);

  uint32_t freed_count = 0;

  // Scan all pages and free those belonging to this process
  for (uint32_t i = 0; i < total_page_count; i++) {
    if (page_infos[i].pid == pid) {
      // Clear page contents for security
      omemset((void *)page_infos[i].addr, 0, PAGE_SIZE);

      // Mark as free
      page_infos[i].pid = 0;

      // Add to free list
      page_infos[i].next = free_list_head;
      free_list_head = &page_infos[i];

      freed_count++;

      TRACE_MEM("Freed page %x from pid %d", page_infos[i].addr, pid);
    }
  }

  // Update statistics
  mem_stats.allocated_pages -= freed_count;
  mem_stats.freed_pages += freed_count;

  TRACE_MEM("Freed %d pages from pid %d", freed_count, pid);
}

void memory_report() {
  oprintf("\n=== Memory Statistics ===\n");
  oprintf("Total pages: %d\n", mem_stats.total_pages);
  oprintf("Total processes created: %d\n", mem_stats.processes_created);
  oprintf("Current allocated pages: %d\n", mem_stats.allocated_pages);
  oprintf("Total pages freed: %d\n", mem_stats.freed_pages);
  oprintf("Peak memory usage: %d pages\n", mem_stats.peak_usage_pages);
  oprintf("Current memory usage: %d KB\n",
          (mem_stats.allocated_pages * PAGE_SIZE) / 1024);
  oprintf("=========================\n");
}

void memory_increment_process_count() {
  mem_stats.processes_created++;
}