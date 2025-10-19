// kernel-prog.cpp - Test programs for the kernel

#include "otk/kernel.hpp"

// a basic process that just prints hello world and exits
extern "C" void proc_hello_world(void) {
  oprintf("TEST: Hello, world!\n");
  current_proc->state = TERMINATED;
  yield();
}

// Minimal binary image for memory test - just a few bytes to allocate a page
#if KERNEL_PROG == KERNEL_PROG_TEST_MEM
const char mem_test_image[] = {
    0x01, 0x00, 0x00, 0x00, // Minimal data
    0x00, 0x00, 0x00, 0x00,
};
#endif

// Test process for memory recycling - just does minimal work and exits
extern "C" void proc_mem_test(void) {
  oprintf("TEST: Process %d running\n", current_proc->pid);
  current_proc->state = TERMINATED;
  yield();
}

// Helper to get all pages allocated to a process
void get_process_pages(uint32_t pid, uintptr_t *pages, uint32_t *count) {
  extern PageInfo *page_infos;
  extern uint32_t total_page_count;

  *count = 0;
  for (uint32_t i = 0; i < total_page_count && *count < 16; i++) {
    if (page_infos[i].pid == pid) {
      pages[(*count)++] = page_infos[i].addr;
    }
  }
}

// TEST_ALTERNATE: Process A - outputs 1, yields, outputs 3
extern "C" void proc_alternate_a(void) {
  while (1) {
    oprintf("A\n");
    yield();
  }
  current_proc->state = TERMINATED;
}

// TEST_ALTERNATE: Process B - outputs 2, yields, outputs 4
extern "C" void proc_alternate_b(void) {
  while (1) {
    oprintf("B\n");
    yield();
  }
  current_proc->state = TERMINATED;
}
