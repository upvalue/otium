#include "otk/kernel.hpp"

extern "C" char __bss[], __bss_end[], __stack_top[];

#ifndef OT_ARCH_WASM
extern "C" char _binary_otu_prog_shell_bin_start[],
    _binary_otu_prog_shell_bin_size[];
#else
// For WASM, we compile the shell together with the kernel
extern "C" void shell_main(void);  // Shell main function
#endif

// Forward declarations for test programs (defined in kernel-prog.cpp)
extern "C" void proc_hello_world(void);
extern "C" void proc_mem_test(void);
extern "C" void proc_alternate_a(void);
extern "C" void proc_alternate_b(void);
extern const char mem_test_image[];
void get_process_pages(uint32_t pid, uintptr_t *pages, uint32_t *count);

void kernel_common(void) {
#ifndef OT_ARCH_WASM
  omemset(__bss, 0, (size_t)__bss_end - (size_t)__bss);
#endif
  TRACE("hello from kernel_common");

  idle_proc = process_create("idle", nullptr, 0, false);
  current_proc = idle_proc;
  TRACE("created idle proc with name %s and pid %d", idle_proc->name,
        idle_proc->pid);

#ifdef KERNEL_PROG_TEST_MEM
  // Test mode: memory recycling test
  oprintf("TEST: Starting memory recycling test\n");

  // Create first process with minimal image to allocate user pages
  Process *proc1 = process_create("mem_test_1", mem_test_image,
                                  sizeof(mem_test_image), true);
  uintptr_t proc1_pages[16];
  uint32_t proc1_page_count = 0;
  get_process_pages(proc1->pid, proc1_pages, &proc1_page_count);
  oprintf("TEST: Process 1 (pid %d) allocated %d pages\n", proc1->pid,
          proc1_page_count);

  // Create second process
  Process *proc2 = process_create("mem_test_2", mem_test_image,
                                  sizeof(mem_test_image), true);
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
                                  sizeof(mem_test_image), true);
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

#elif defined(KERNEL_PROG_TEST_HELLO)
  // Test mode: run hello world test
  Process *test_proc =
      process_create("test_hello", (const void *)proc_hello_world, 0, false);
  TRACE("created test proc with name %s and pid %d", test_proc->name,
        test_proc->pid);
#elif defined(KERNEL_PROG_TEST_ALTERNATE)
  // Test mode: alternate process execution
  oprintf("TEST: Starting alternate process test (should print 1234)\n");
  Process *proc_a =
      process_create("alternate_a", (const void *)proc_alternate_a, 0, false);
  Process *proc_b =
      process_create("alternate_b", (const void *)proc_alternate_b, 0, false);
  TRACE("created proc_a with name %s and pid %d", proc_a->name, proc_a->pid);
  TRACE("created proc_b with name %s and pid %d", proc_b->name, proc_b->pid);
#else
  // Default/main mode
#ifdef OT_ARCH_WASM
  // For WASM, call the shell main function directly
  Process *proc_shell = process_create("shell", (const void *)shell_main, 0, false);
#else
  // For RISC-V, load the shell from the embedded binary
  Process *proc_shell =
      process_create("shell", (const void *)_binary_otu_prog_shell_bin_start,
                     (size_t)_binary_otu_prog_shell_bin_size, true);
#endif
  TRACE("created proc with name %s and pid %d", proc_shell->name,
        proc_shell->pid);
#endif

#ifdef OT_ARCH_WASM
  // For WASM: use explicit scheduler loop
  scheduler_loop();
#else
  // For RISC-V: yield and let processes run
  yield();
#endif

  TRACE("no programs left to run, exiting kernel\n");
  memory_report();
  kernel_exit();
}
