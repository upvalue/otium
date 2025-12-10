// main.cpp - Kernel startup logic

#include "ot/core/kernel.hpp"
#include "ot/user/user.hpp"

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

/**
 * the default kernel program (actually run the system)
 */
void kernel_prog_default() {
  // Fibonacci server is only created for IPC tests, not for default program

#if OT_GRAPHICS_BACKEND != OT_GRAPHICS_BACKEND_NONE
  // create graphics driver (proc_graphics is defined in ot/user/graphics/impl.cpp)
  extern void proc_graphics(void);
  process_create("graphics", (const void *)proc_graphics, nullptr, false);
#endif

  oprintf("OT_FILESYSTEM_BACKEND: %d\n", OT_FILESYSTEM_BACKEND);
#if OT_FILESYSTEM_BACKEND != OT_FILESYSTEM_BACKEND_NONE
  // create filesystem server (proc_filesystem is defined in ot/user/filesystem/impl.cpp)
  extern void proc_filesystem(void);
  process_create("filesystem", (const void *)proc_filesystem, nullptr, false);
#endif

#if OT_KEYBOARD_BACKEND != OT_KEYBOARD_BACKEND_NONE
  // create keyboard driver (proc_keyboard is defined in ot/user/keyboard/impl.cpp)
  extern void proc_keyboard(void);
  process_create("keyboard", (const void *)proc_keyboard, nullptr, false);
#endif

#ifdef ENABLE_SHELL
  // Create shell process - choose UI shell or text shell based on kernel program
#if KERNEL_PROG == KERNEL_PROG_UISHELL
  char *shell_argv[] = {(char *)"uishell"};
  Arguments shell_args = {1, shell_argv};
  process_create("uishell", (const void *)user_program_main, &shell_args, false);
#else
  char *shell_argv[] = {(char *)"shell"};
  Arguments shell_args = {1, shell_argv};
  process_create("shell", (const void *)user_program_main, &shell_args, false);
#endif
#endif

  // Start typedemo (keyboard typing demo) when graphics and keyboard are enabled (but not for uishell mode)
#if OT_GRAPHICS_BACKEND != OT_GRAPHICS_BACKEND_NONE && OT_KEYBOARD_BACKEND != OT_KEYBOARD_BACKEND_NONE &&              \
    KERNEL_PROG != KERNEL_PROG_UISHELL
#if 0
  char *typedemo_argv[] = {(char *)"typedemo"};
  Arguments typedemo_args = {1, typedemo_argv};
  process_create("typedemo", (const void *)user_program_main, &typedemo_args, false);
#endif

  // char *spacedemo_argv[] = {(char *)"spacedemo"};
  // Arguments spacedemo_args = {1, spacedemo_argv};
  // process_create("spacedemo", (const void *)user_program_main, &spacedemo_args, false);
#endif
}

// Kernel startup - initializes kernel and creates initial processes
void kernel_start(void) {
  kernel_common();

  // Kernel "program" there are a few different programs for testing some functionality
  // of the kernel end to end

#if KERNEL_PROG == KERNEL_PROG_DEFAULT || KERNEL_PROG == KERNEL_PROG_SHELL || KERNEL_PROG == KERNEL_PROG_UISHELL
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