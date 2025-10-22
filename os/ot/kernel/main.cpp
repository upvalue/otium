// kernel-prog.cpp - Kernel startup logic and test programs

#include "ot/kernel/kernel.hpp"

// Forward declaration for kernel_common (defined in startup.cpp)
void kernel_common(void);

#ifdef OT_ARCH_WASM
// Forward declaration for shell_main (defined in prog-user.cpp)
extern "C" void user_program_main(void);
#else
extern "C" char _binary_bin_prog_shell_bin_start[],
    _binary_bin_prog_shell_bin_size[];
#endif

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

// Helper to get all pages allocated to a process
void get_process_pages(int32_t pid, uintptr_t *pages, uint32_t *count) {
  extern PageInfo *page_infos;
  extern uint32_t total_page_count;

  *count = 0;
  for (uint32_t i = 0; i < total_page_count && *count < 16; i++) {
    if (page_infos[i].pid == pid) {
      pages[(*count)++] = page_infos[i].addr.raw();
    }
  }
}

// Kernel startup - initializes kernel and creates initial processes
void kernel_start(void) {
  kernel_common();

#if KERNEL_PROG == KERNEL_PROG_TEST_MEM
  // Test mode: memory recycling test
  oprintf("TEST: Starting memory recycling test\n");

  // Create first process with minimal image to allocate user pages
  Process *proc1 = process_create("mem_test_1", mem_test_image,
                                  sizeof(mem_test_image), true, nullptr);
  uintptr_t proc1_pages[16];
  uint32_t proc1_page_count = 0;
  get_process_pages(proc1->pid, proc1_pages, &proc1_page_count);
  oprintf("TEST: Process 1 (pid %d) allocated %d pages\n", proc1->pid,
          proc1_page_count);

  // Create second process
  Process *proc2 = process_create("mem_test_2", mem_test_image,
                                  sizeof(mem_test_image), true, nullptr);
  uintptr_t proc2_pages[16];
  uint32_t proc2_page_count = 0;
  get_process_pages(proc2->pid, proc2_pages, &proc2_page_count);
  oprintf("TEST: Process 2 (pid %d) allocated %d pages\n", proc2->pid,
          proc2_page_count);

  // Exit process 1 to free its pages
  process_exit(proc1);
  oprintf("TEST: Exited process 1 (freed %d pages)\n", proc1_page_count);

  // Create third process - should reuse process 1's pages
  Process *proc3 = process_create("mem_test_3", mem_test_image,
                                  sizeof(mem_test_image), true, nullptr);
  uintptr_t proc3_pages[16];
  uint32_t proc3_page_count = 0;
  get_process_pages(proc3->pid, proc3_pages, &proc3_page_count);
  oprintf("TEST: Process 3 (pid %d) allocated %d pages\n", proc3->pid,
          proc3_page_count);

  // Verify page recycling - check if all of proc3's pages are from proc1
  uint32_t reused_count = 0;
  for (uint32_t i = 0; i < proc3_page_count; i++) {
    for (uint32_t j = 0; j < proc1_page_count; j++) {
      if (proc3_pages[i] == proc1_pages[j]) {
        reused_count++;
        break;
      }
    }
  }

  if (reused_count == proc3_page_count &&
      proc3_page_count == proc1_page_count) {
    oprintf("TEST: SUCCESS - Process 3 reused all %d pages from Process 1\n",
            reused_count);
  } else {
    oprintf("TEST: FAILURE - Process 3 reused %d/%d pages (expected %d)\n",
            reused_count, proc3_page_count, proc1_page_count);
  }

  // Clean up
  process_exit(proc2);
  process_exit(proc3);

#elif KERNEL_PROG == KERNEL_PROG_TEST_HELLO
  // Test mode: run hello world test
  Process *test_proc = process_create(
      "test_hello", (const void *)proc_hello_world, 0, false, nullptr);
  TRACE(LSOFT, "created test proc with name %s and pid %d", test_proc->name,
        test_proc->pid);
#elif KERNEL_PROG == KERNEL_PROG_TEST_ALTERNATE
  // Test mode: alternate process execution
  oprintf("TEST: Starting alternate process test (should print 1234)\n");
  Process *proc_a = process_create(
      "alternate_a", (const void *)proc_alternate_a, 0, false, nullptr);
  Process *proc_b = process_create(
      "alternate_b", (const void *)proc_alternate_b, 0, false, nullptr);
  TRACE(LSOFT, "created proc_a with name %s and pid %d", proc_a->name,
        proc_a->pid);
  TRACE(LSOFT, "created proc_b with name %s and pid %d", proc_b->name,
        proc_b->pid);
#else
  // Default/main mode
  char *shell_argv = {"shell"};
  Arguments shell_args = {1, &shell_argv};
  char *scratch_argv = {"scratch"};
  Arguments scratch_args = {1, &scratch_argv};
#ifdef OT_ARCH_WASM
  // For WASM, call the shell main function directly
  Process *proc_shell = process_create("shell", (const void *)user_program_main,
                                       0, false, &shell_args);

  Process *proc_scratch = process_create(
      "scratch", (const void *)user_program_main, 0, false, &scratch_args);
#else
  // For RISC-V, load the shell from the embedded binary
  Process *proc_shell = process_create(
      "shell", (const void *)_binary_bin_prog_shell_bin_start,
      (size_t)_binary_bin_prog_shell_bin_size, true, &shell_args);

  Process *proc_scratch = process_create(
      "scratch", (const void *)_binary_bin_prog_shell_bin_start,
      (size_t)_binary_bin_prog_shell_bin_size, true, &scratch_args);
#endif
  TRACE(LSOFT, "created proc with name %s and pid %d", proc_shell->name,
        proc_shell->pid);
#endif

#ifdef OT_ARCH_WASM
  // For WASM: use explicit scheduler loop
  scheduler_loop();
#else
  // For RISC-V: yield and let processes run
  yield();
#endif

  TRACE(LSOFT, "no programs left to run, exiting kernel");
  memory_report();
  kernel_exit();
}