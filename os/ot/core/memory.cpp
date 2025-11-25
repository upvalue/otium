// memory.cpp - page manager

#include "ot/core/kernel.hpp"

static PageAddr next_page_addr = PageAddr(__free_ram);

// Page tracking for recycling
PageInfo *page_infos = nullptr;
static PageInfo *free_list_head = nullptr;
static MemoryStats mem_stats = {0, 0, 0, 0, 0};
static bool memory_initialized = false;
uint32_t total_page_count = 0;

// Known memory regions - globally reserved contiguous memory
KnownMemoryInfo known_memory_table[KNOWN_MEMORY_COUNT];

// Bootstrap allocator - only used during memory_init to allocate PageInfo array
static PageAddr page_allocate_bootstrap(size_t page_count) {
  PageAddr page_addr = next_page_addr;
  next_page_addr += page_count * OT_PAGE_SIZE;

  if (next_page_addr.raw() > (uintptr_t)__free_ram_end) {
    PANIC("out of memory during bootstrap");
  }

  TRACE_MEM(LLOUD, "Bootstrap allocated %d pages at address %x", page_count,
            page_addr.raw());

  omemset(page_addr.as_ptr(), 0, page_count * OT_PAGE_SIZE);
  return page_addr;
}

void memory_init() {
  if (memory_initialized) {
    return;
  }

  TRACE(LSOFT, "Initializing memory management system");

  // Calculate total number of pages available
  uintptr_t free_ram_size = (uintptr_t)__free_ram_end - (uintptr_t)__free_ram;
  total_page_count = free_ram_size / OT_PAGE_SIZE;

  TRACE(LSOFT, "Total pages available: %d", total_page_count);

  // Allocate PageInfo array using bootstrap allocator (before tracking starts)
  size_t page_infos_size = total_page_count * sizeof(PageInfo);
  size_t page_infos_pages = (page_infos_size + OT_PAGE_SIZE - 1) / OT_PAGE_SIZE;
  PageAddr page_infos_addr = page_allocate_bootstrap(page_infos_pages);
  page_infos = page_infos_addr.as<PageInfo>();

  TRACE(LSOFT, "Allocated %d pages for PageInfo array at %x", page_infos_pages,
        page_infos_addr.raw());

  // Initialize page tracking structures
  PageAddr current_page_addr = next_page_addr;
  PageInfo *prev = nullptr;

  for (uint32_t i = 0; i < total_page_count; i++) {
    PageAddr page_addr = PageAddr((uintptr_t)__free_ram + i * OT_PAGE_SIZE);

    // Skip pages already allocated for PageInfo array
    if (page_addr.raw() < current_page_addr.raw()) {
      page_infos[i].pidx = Pidx(-1); // Mark as kernel/system page
      page_infos[i].addr = page_addr;
      page_infos[i].next = nullptr;
      continue;
    }

    // Initialize as free page
    page_infos[i].pidx = PIDX_NONE;
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

  // Initialize known memory regions (must happen early before fragmentation)
  known_memory_init();

  TRACE(LSOFT, "Memory initialization complete. Free list head: %x",
        free_list_head);
}

PageAddr page_allocate(Pidx pidx, size_t page_count) {
  if (!memory_initialized) {
    PANIC("page_allocate called before memory_init");
  }

  TRACE_MEM(LLOUD, "page_allocate: pidx=%d, count=%d", pidx.raw(), page_count);

  if (page_count == 0) {
    PANIC("Cannot allocate 0 pages");
  }

  // Check if we have enough free pages
  PageInfo *check = free_list_head;
  size_t available = 0;
  while (check && available < page_count) {
    available++;
    check = check->next;
  }

  if (available < page_count) {
    PANIC("Out of memory - requested %d pages, only %d available", page_count,
          available);
  }

  // Allocate first page (this is what we'll return)
  PageInfo *first_page = free_list_head;
  free_list_head = first_page->next;
  first_page->pidx = pidx;
  first_page->next = nullptr;
  omemset(first_page->addr.as_ptr(), 0, OT_PAGE_SIZE);

  TRACE_MEM(LLOUD, "Allocated page at %x to pidx %d", first_page->addr.raw(),
            pidx.raw());

  // Allocate remaining pages
  for (size_t i = 1; i < page_count; i++) {
    PageInfo *page_info = free_list_head;
    free_list_head = page_info->next;
    page_info->pidx = pidx;
    page_info->next = nullptr;
    omemset(page_info->addr.as_ptr(), 0, OT_PAGE_SIZE);

    TRACE_MEM(LLOUD, "Allocated page at %x to pidx %d", page_info->addr.raw(),
              pidx.raw());
  }

  // Update statistics
  mem_stats.allocated_pages += page_count;
  if (mem_stats.allocated_pages > mem_stats.peak_usage_pages) {
    mem_stats.peak_usage_pages = mem_stats.allocated_pages;
  }

  return first_page->addr;
}

PageInfo *page_info_lookup(PageAddr addr) {
  PageInfo *pinfo = page_infos;
  for (uint32_t i = 0; i < total_page_count; i++) {
    if (pinfo[i].addr == addr) {
      return &pinfo[i];
    }
  }
  return nullptr;
}

void page_free_process(Pidx pidx) {
  if (!memory_initialized) {
    TRACE_MEM(LSOFT, "Memory not initialized, cannot free pages");
    return;
  }

  TRACE_MEM(LSOFT, "page_free_process: pidx=%d", pidx.raw());

  uint32_t freed_count = 0;

  // Scan all pages and free those belonging to this process
  for (uint32_t i = 0; i < total_page_count; i++) {
    if (page_infos[i].pidx == pidx) {
      // Clear page contents for security
      omemset(page_infos[i].addr.as_ptr(), 0, OT_PAGE_SIZE);

      // Mark as free
      page_infos[i].pidx = PIDX_NONE;

      // Add to free list
      page_infos[i].next = free_list_head;
      free_list_head = &page_infos[i];

      freed_count++;

      TRACE_MEM(LLOUD, "Freed page %x from pidx %d", page_infos[i].addr.raw(),
                pidx.raw());
    }
  }

  // Update statistics
  mem_stats.allocated_pages -= freed_count;
  mem_stats.freed_pages += freed_count;

  TRACE_MEM(LSOFT, "Freed %d pages from pidx %d", freed_count, pidx.raw());
}

void memory_report() {
  oprintf("\n=== Memory Statistics ===\n");
  oprintf("Total pages: %d\n", mem_stats.total_pages);
  oprintf("Total processes created: %d\n", mem_stats.processes_created);
  oprintf("Current allocated pages: %d\n", mem_stats.allocated_pages);
  oprintf("Total pages freed: %d\n", mem_stats.freed_pages);
  oprintf("Peak memory usage: %d pages\n", mem_stats.peak_usage_pages);
  oprintf("Current memory usage: %d KB\n",
          (mem_stats.allocated_pages * OT_PAGE_SIZE) / 1024);
  oprintf("=========================\n");
}

void memory_increment_process_count() { mem_stats.processes_created++; }

// Known memory management

void known_memory_init() {
  // Initialize all entries to empty - allocation happens on first lock
  for (int i = 0; i < KNOWN_MEMORY_COUNT; i++) {
    known_memory_table[i].addr = PageAddr(nullptr);
    known_memory_table[i].page_count = 0;
    known_memory_table[i].holder_pidx = PIDX_NONE;
  }
}

PageAddr known_memory_lock(KnownMemory km, size_t page_count, Pidx pidx) {
  if (km <= KNOWN_MEMORY_NONE || km >= KNOWN_MEMORY_COUNT) {
    TRACE_MEM(LSOFT, "known_memory_lock: invalid km=%d", km);
    return PageAddr(nullptr);
  }

  KnownMemoryInfo *info = &known_memory_table[km];

  // Check if already locked by another process
  if (info->holder_pidx != PIDX_NONE && info->holder_pidx != pidx) {
    TRACE_MEM(LSOFT, "known_memory_lock: km=%d already held by pidx=%d", km,
              info->holder_pidx.raw());
    return PageAddr(nullptr);
  }

  // Allocate on first lock
  if (info->addr.is_null()) {
    // Use Pidx(-1) to mark as kernel-reserved memory
    PageAddr addr = page_allocate(Pidx(-1), page_count);
    if (addr.is_null()) {
      TRACE_MEM(LSOFT, "known_memory_lock: failed to allocate %d pages", page_count);
      return PageAddr(nullptr);
    }
    info->addr = addr;
    info->page_count = page_count;
    TRACE_MEM(LSOFT, "known_memory_lock: allocated %d pages at %x for km=%d",
              page_count, addr.raw(), km);
  }

  // Check if requested size fits within allocated region
  if (page_count > info->page_count) {
    TRACE_MEM(LSOFT,
              "known_memory_lock: requested %d pages but only %d allocated",
              page_count, info->page_count);
    return PageAddr(nullptr);
  }

  // Lock the memory to this process
  info->holder_pidx = pidx;
  TRACE_MEM(LSOFT, "known_memory_lock: pidx=%d locked km=%d (%d pages) at %x",
            pidx.raw(), km, page_count, info->addr.raw());

  return info->addr;
}

void known_memory_release_process(Pidx pidx) {
  // Release any known memory regions held by this process
  for (int i = 0; i < KNOWN_MEMORY_COUNT; i++) {
    if (known_memory_table[i].holder_pidx == pidx) {
      TRACE_MEM(LSOFT, "Releasing known memory region %d from pidx %d", i,
                pidx.raw());
      known_memory_table[i].holder_pidx = PIDX_NONE;
      // Note: We don't free the memory, just release the lock
    }
  }
}