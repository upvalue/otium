#include "otk/kernel.hpp"

extern "C" char __bss[], __bss_end[], __stack_top[];

extern "C" char _binary_otu_prog_shell_bin_start[],
    _binary_otu_prog_shell_bin_size[];

// a basic process that just prints hello world and exits
extern "C" void proc_hello_world(void) {
  oprintf("TEST: Hello, world!\n");
  current_proc->state = TERMINATED;
  yield();
}

void kernel_common(void) {
  omemset(__bss, 0, (size_t)__bss_end - (size_t)__bss);
  TRACE("hello from kernel_common");

  idle_proc = process_create("idle", nullptr, 0, false);
  current_proc = idle_proc;
  TRACE("created idle proc with name %s and pid %d", idle_proc->name,
        idle_proc->pid);

#ifdef KERNEL_PROG_TEST_HELLO
  // Test mode: run hello world test
  Process *test_proc =
      process_create("test_hello", (const void *)proc_hello_world, 0, false);
  TRACE("created test proc with name %s and pid %d", test_proc->name,
        test_proc->pid);
#else
  // Default/main mode
  Process *proc_shell =
      process_create("shell", (const void *)_binary_otu_prog_shell_bin_start,
                     (size_t)_binary_otu_prog_shell_bin_size, true);
  TRACE("created proc with name %s and pid %d", proc_shell->name,
        proc_shell->pid);
#endif

  yield();
  TRACE("no programs left to run, exiting kernel\n");
  kernel_exit();
}
