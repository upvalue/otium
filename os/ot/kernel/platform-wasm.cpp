// platform-wasm.cpp - WASM/Emscripten specific functionality

#include "ot/kernel/kernel.hpp"
#include <emscripten.h>
#include <emscripten/fiber.h>
#include <stdlib.h>

// Memory region for WASM (simulate __free_ram from linker script)
// Allocate 16MB for OS use
static char wasm_ram[16 * 1024 * 1024];

// Define the memory region symbols that the kernel expects
extern "C" {
char *__free_ram = wasm_ram;
char *__free_ram_end = wasm_ram + sizeof(wasm_ram);
}

// Console I/O buffer for getchar
static char input_buffer[256];
static int input_read_pos = 0;
static int input_write_pos = 0;

// JavaScript imports for console I/O
extern "C" {
// These will be provided by the JavaScript environment
// clang-format off
EM_JS(void, js_putchar, (char ch), {
  const str = String.fromCharCode(ch);
  if (typeof Module.print2 === 'function') {
    // Use Module.print if available (our custom print function)
    Module.print2(str, false);
  } else {
    // Fallback to console.log for each character
    if (ch === 10) { // newline
      // console.log("");
    } else {
      // In browser, just use console.log without newline
      // console.log(str);
    }
  }
});

EM_JS(int, js_getchar, (), {
  // Check if there's a character available in the JS input buffer
  if (Module.inputBuffer && Module.inputBuffer.length > 0) {
    return Module.inputBuffer.shift();
  }
  return -1;
});

EM_JS(void, js_exit, (), {
  Module.exit();
});

// clang-format on

} // end extern "C" for EM_JS

// Console I/O functions - must be extern "C" for user programs
extern "C" {

void oputchar(char ch) { js_putchar(ch); }

int ogetchar() {
  emscripten_sleep(0);
  // Check local buffer first
  if (input_read_pos != input_write_pos) {
    char ch = input_buffer[input_read_pos];
    input_read_pos = (input_read_pos + 1) % sizeof(input_buffer);
    return ch;
  }

  // Try to get from JavaScript
  int ch = js_getchar();
  if (ch >= 0) {
    return ch;
  }

  // No character available
  return -1;
}

} // end extern "C" for console I/O

void kernel_exit(void) {
  oprintf("Kernel exiting\n");
  js_exit();
}

void wfi(void) {
  // In WASM, just infinite loop (idle)
  while (1) {
    // Busy wait
  }
}

// User entry - for WASM, we don't need privilege mode switching
// Just call the user program directly
extern "C" void user_entry(void) {
  // Jump to user base address
  TRACE(LLOUD, "user_entry: calling user program for process %s",
        current_proc->name);
  typedef void (*user_func_t)(void);
  user_func_t user_main = (user_func_t)current_proc->user_pc;
  user_main();

  // If user program returns, exit the process
  TRACE(LLOUD, "user_entry: user program %s returned, marking TERMINATED",
        current_proc->name);
  current_proc->state = TERMINATED;
  yield();
}

// Scheduler fiber
static emscripten_fiber_t scheduler_fiber;
static void *scheduler_asyncify_stack = nullptr;

void yield(void) {
  emscripten_sleep(0);

  if (!current_proc || !idle_proc) {
    PANIC("current_proc or idle_proc is null");
  }

  TRACE(LLOUD, "yield: process %s (pid=%d) yielding", current_proc->name,
        current_proc->pid);

  // Switch from process fiber back to scheduler fiber
  // First arg is current context, second is target context
  emscripten_fiber_swap((emscripten_fiber_t *)current_proc->fiber,
                        &scheduler_fiber);

  TRACE(LLOUD, "yield: process %s (pid=%d) resumed", current_proc->name,
        current_proc->pid);
}

// Fiber entry point wrapper - calls user_entry for the process
static void fiber_entry_point(void *arg) {
  Process *proc = (Process *)arg;
  TRACE(LLOUD, "fiber_entry_point: starting process %s (pid=%d)", proc->name,
        proc->pid);

  // Set as current process and run
  current_proc = proc;
  user_entry();

  // If we get here, process terminated (returned from user_entry instead of
  // yielding)
  TRACE(LLOUD,
        "fiber_entry_point: process %s returned from user_entry, marking "
        "TERMINATED",
        proc->name);
  proc->state = TERMINATED;

  // Yield back to scheduler
  yield();
}

// WASM scheduler loop - runs processes cooperatively with fibers
void scheduler_loop(void) {
  TRACE(LSOFT, "Entering WASM scheduler loop");

  // Initialize the scheduler fiber from current context
  const size_t SCHEDULER_ASYNCIFY_STACK_SIZE = 512 * 1024; // 512KB
  scheduler_asyncify_stack = malloc(SCHEDULER_ASYNCIFY_STACK_SIZE);
  if (!scheduler_asyncify_stack) {
    PANIC("Failed to allocate scheduler asyncify stack");
  }

  TRACE(LSOFT, "Initializing scheduler fiber with asyncify stack size %d",
        SCHEDULER_ASYNCIFY_STACK_SIZE);
  emscripten_fiber_init_from_current_context(&scheduler_fiber,
                                             scheduler_asyncify_stack,
                                             SCHEDULER_ASYNCIFY_STACK_SIZE);

  while (true) {
    Process *next = process_next_runnable();

    // Check if we're done (only idle process left or no processes)
    if (!next || next == idle_proc) {
      TRACE(LSOFT, "No more runnable processes, exiting scheduler");
      break;
    }

    TRACE(LLOUD, "Scheduler picked process %s (pid=%d)", next->name, next->pid);
    current_proc = next;

    // Create fiber for this process if not already created
    if (!next->started) {
      next->started = true;

      const size_t C_STACK_SIZE = 512 * 1024;        // 512KB C stack
      const size_t ASYNCIFY_STACK_SIZE = 512 * 1024; // 512KB asyncify stack

      TRACE(LLOUD,
            "Creating fiber for process %s with stack size %d, asyncify stack "
            "size %d",
            next->name, C_STACK_SIZE, ASYNCIFY_STACK_SIZE);

      // Allocate stacks dynamically
      void *c_stack = malloc(C_STACK_SIZE);
      void *asyncify_stack = malloc(ASYNCIFY_STACK_SIZE);

      if (!c_stack || !asyncify_stack) {
        PANIC("Failed to allocate stacks for fiber");
      }

      // Allocate and initialize fiber
      next->fiber = new emscripten_fiber_t;
      emscripten_fiber_init((emscripten_fiber_t *)next->fiber,
                            fiber_entry_point, next, c_stack, C_STACK_SIZE,
                            asyncify_stack, ASYNCIFY_STACK_SIZE);
    }

    // Swap to the process fiber
    // First arg is current context (scheduler), second is target (process)
    TRACE(LLOUD, "Swapping to process %s (state=%d)", next->name, next->state);
    emscripten_fiber_swap(&scheduler_fiber, (emscripten_fiber_t *)next->fiber);
    TRACE(LLOUD, "Returned from process %s (state=%d)", next->name,
          next->state);
  }

  TRACE(LSOFT, "Scheduler loop finished");

  // Cleanup
  if (scheduler_asyncify_stack) {
    free(scheduler_asyncify_stack);
    scheduler_asyncify_stack = nullptr;
  }
}

// Syscall handlers for user programs
extern "C" {
;
void kernel_syscall_putchar(char ch) {
  oputchar(ch);
  yield();
}

int kernel_syscall_getchar(void) {
  int ch = ogetchar();
  yield();
  return ch;
}

void kernel_syscall_yield(void) { yield(); }

void kernel_syscall_exit(void) {
  js_exit();
  if (current_proc) {
    oprintf("Process %s (pid=%d) exited\n", current_proc->name,
            current_proc->pid);
    current_proc->state = TERMINATED;
  }
  yield();
}

void *kernel_syscall_alloc_page(void) {
  Pair<PageAddr, PageAddr> result =
      process_alloc_mapped_page(current_proc, true, true, false);
  yield();
  return result.second.as_ptr();
}

PageAddr kernel_syscall_get_arg_page(void) { return process_get_arg_page(); }

// Main entry point for WASM
extern "C" void kernel_main(void) {
  oprintf("Otium OS starting on WASM\n");
  kernel_start();
}

// Emscripten calls main, so we just call kernel_main
int main() {
  kernel_main();
  return 0;
}
