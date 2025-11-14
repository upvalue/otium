// kernel-prog.cpp - Kernel startup logic and test programs

#include "ot/core/kernel.hpp"
#include "ot/drivers/drv-gfx-virtio.hpp"

// Forward declaration for kernel_common (defined in startup.cpp)
void kernel_common(void);

// Forward declaration for user_program_main (defined in user-main.cpp)
extern "C" void user_program_main(void);

// a basic process that just prints hello world and exits
extern "C" void proc_hello_world(void) {
  oprintf("TEST: Hello, world!\n");
  // For now, loop forever - we'll add proper exit via syscall later
  while (1) {
    asm volatile("wfi"); // Wait for interrupt
  }
}

// Memory test now uses proc_mem_test function pointer (no binary image needed)

// Test process for memory recycling - just does minimal work and exits
extern "C" void proc_mem_test(void) {
  oprintf("TEST: Process %d running\n", current_proc->pid);
  current_proc->state = TERMINATED;
  yield();
}

// TEST_ALTERNATE: Process A - outputs 1, yields, outputs 3
extern "C" void proc_alternate_a(void) {
  oputchar('1');
  yield();
  oputchar('3');
  yield();
  current_proc->state = TERMINATED;
  yield();
}

// TEST_ALTERNATE: Process B - outputs 2, yields, outputs 4
extern "C" void proc_alternate_b(void) {
  oputchar('2');
  yield();
  oputchar('4');
  yield();
  current_proc->state = TERMINATED;
  yield();
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

bool programs_running() {
  // start at 1 to skip idle proc
  for (int i = 1; i < PROCS_MAX; i++) {
    if (procs[i].state == RUNNABLE) {
      return true;
    }
  }
  return false;
}

// Kernel startup - initializes kernel and creates initial processes
void kernel_start(void) {
  kernel_common();

#if KERNEL_PROG == KERNEL_PROG_TEST_MEM
  // Test mode: memory recycling test
  oprintf("TEST: Starting memory recycling test\n");

  // Create first process
  Process *proc1 = process_create("mem_test_1", (const void *)proc_mem_test, nullptr);
  uintptr_t proc1_pages[16];
  uint32_t proc1_page_count = 0;
  get_process_pages(proc1->pid, proc1_pages, &proc1_page_count);
  oprintf("TEST: Process 1 (pid %d) allocated %d pages\n", proc1->pid, proc1_page_count);

  // Create second process
  Process *proc2 = process_create("mem_test_2", (const void *)proc_mem_test, nullptr);
  uintptr_t proc2_pages[16];
  uint32_t proc2_page_count = 0;
  get_process_pages(proc2->pid, proc2_pages, &proc2_page_count);
  oprintf("TEST: Process 2 (pid %d) allocated %d pages\n", proc2->pid, proc2_page_count);

  // Exit process 1 to free its pages
  process_exit(proc1);
  oprintf("TEST: Exited process 1 (freed %d pages)\n", proc1_page_count);

  // Create third process - should reuse process 1's pages
  Process *proc3 = process_create("mem_test_3", (const void *)proc_mem_test, nullptr);
  uintptr_t proc3_pages[16];
  uint32_t proc3_page_count = 0;
  get_process_pages(proc3->pid, proc3_pages, &proc3_page_count);
  oprintf("TEST: Process 3 (pid %d) allocated %d pages\n", proc3->pid, proc3_page_count);

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

  if (reused_count == proc3_page_count && proc3_page_count == proc1_page_count) {
    oprintf("TEST: SUCCESS - Process 3 reused all %d pages from Process 1\n", reused_count);
  } else {
    oprintf("TEST: FAILURE - Process 3 reused %d/%d pages (expected %d)\n", reused_count, proc3_page_count,
            proc1_page_count);
  }

  // Clean up
  process_exit(proc2);
  process_exit(proc3);

#elif KERNEL_PROG == KERNEL_PROG_TEST_HELLO
  // Test mode: run hello world test
  Process *test_proc = process_create("test_hello", (const void *)proc_hello_world, nullptr);
  TRACE(LSOFT, "created test proc with name %s and pid %d", test_proc->name, test_proc->pid);
#elif KERNEL_PROG == KERNEL_PROG_TEST_ALTERNATE
  // Test mode: alternate process execution
  oprintf("TEST: Starting alternate process test (should print 1234)\n");
  Process *proc_a = process_create("alternate_a", (const void *)proc_alternate_a, nullptr);
  Process *proc_b = process_create("alternate_b", (const void *)proc_alternate_b, nullptr);
  TRACE(LSOFT, "created proc_a with name %s and pid %d", proc_a->name, proc_a->pid);
  TRACE(LSOFT, "created proc_b with name %s and pid %d", proc_b->name, proc_b->pid);
  oprintf("TEST: ");
#else
  // Default/main mode
  char *shell_argv = {"shell"};
  Arguments shell_args = {1, &shell_argv};
  char *scratch_argv = {"scratch"};
  Arguments scratch_args = {1, &scratch_argv};
  char *scratch2_argv = {"scratch2"};
  Arguments scratch2_args = {1, &scratch2_argv};
  char *print_server_argv = {"print-server"};
  Arguments print_server_args = {1, &print_server_argv};

  // Create processes by calling user_program_main as a function pointer
  // The user-main.cpp dispatcher will route to the appropriate program based on arguments
  Process *proc_shell = process_create("shell", (const void *)user_program_main, &shell_args);

  Process *proc_print_server =
      process_create("print-server", (const void *)user_program_main, &print_server_args);
  // Process *proc_scratch = process_create("scratch", (const void *)user_program_main, &scratch_args);
  // TRACE(LSOFT, "created proc with name %s and pid %d", proc_shell->name,
//        proc_shell->pid);
#endif

#ifdef OT_ARCH_WASM
  // For WASM: use explicit scheduler loop
  scheduler_loop();
#else
  // For RISC-V: yield and let processes run
  yield();
#endif

  OT_SOFT_ASSERT("reached end of kernel while programs were running", !programs_running());

  oputchar('\n');
  TRACE(LSOFT, "no programs left to run, exiting kernel");
  memory_report();
  kernel_exit();
}