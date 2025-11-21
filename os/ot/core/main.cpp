// main.cpp - Kernel startup logic

#include "ot/core/kernel.hpp"
#include "ot/user/user.hpp"

#include "ot/user/gen/fibonacci-server.hpp"

// Forward declaration for kernel_common (defined in startup.cpp)
void kernel_common(void);

// Forward declaration for user_program_main (defined in user-main.cpp)
void user_program_main(void);

// Forward declaration for test entry point (defined in kernel-tests.cpp)
void kernel_prog_test(void);

bool programs_running() {
  // start at 1 to skip idle proc
  for (int i = 1; i < PROCS_MAX; i++) {
    if (procs[i].state == RUNNABLE) {
      return true;
    }
  }
  return false;
}

void proc_fibonacci_server(void) {
  oprintf("Fibonacci server started\n");
  FibonacciServer server;
  server.run();
}

/**
 * the default kernel program (actually run the system)
 */
void kernel_prog_default() {
  // create fibonacci ipc server
  Process *proc_fib = process_create("fib", (const void *)proc_fibonacci_server, nullptr, false);

#if OT_GRAPHICS_BACKEND != OT_GRAPHICS_BACKEND_NONE
  // create graphics driver (proc_graphics is defined in ot/user/graphics/impl.cpp)
  extern void proc_graphics(void);
  process_create("graphics", (const void *)proc_graphics, nullptr, false);
#endif

#if OT_FILESYSTEM_BACKEND != OT_FILESYSTEM_BACKEND_NONE
  // create filesystem server (proc_filesystem is defined in ot/user/filesystem/impl.cpp)
  extern void proc_filesystem(void);
  process_create("filesystem", (const void *)proc_filesystem, nullptr, false);
#endif

#ifdef ENABLE_SHELL
  // create shell process
  char *shell_argv[] = {(char *)"shell"};
  Arguments shell_args = {1, shell_argv};
  process_create("shell", (const void *)user_program_main, &shell_args, false);
#endif

  // Uncomment to enable spacedemo (graphics demo)
  // char *spacedemo_argv[] = {(char *)"spacedemo"};
  // Arguments spacedemo_args = {1, spacedemo_argv};
  // process_create("spacedemo", (const void *)user_program_main, &spacedemo_args, false);
}

// Kernel startup - initializes kernel and creates initial processes
void kernel_start(void) {
  kernel_common();

  // Kernel "program" there are a few different programs for testing some functionality
  // of the kernel end to end

#if KERNEL_PROG == KERNEL_PROG_DEFAULT
  kernel_prog_default();
#else
  kernel_prog_test();
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