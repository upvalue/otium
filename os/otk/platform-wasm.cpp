// platform-wasm.cpp - WASM/Emscripten specific functionality

#include "otk/kernel.hpp"
#include <emscripten.h>

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
  // emscripten_force_exit(0);
}

void wfi(void) {
  // In WASM, just yield execution and wait
  while (1) {
    emscripten_sleep(100);
  }
}

// For WASM, switch_context just needs to yield to the event loop
// Asyncify will handle saving/restoring the call stack
extern "C" void switch_context(uintptr_t *prev_sp, uintptr_t *next_sp) {
  // In WASM with Asyncify, we just need to yield to the event loop
  // The stack will be automatically saved and restored by Asyncify
  (void)prev_sp; // Unused in WASM
  (void)next_sp; // Unused in WASM

  // This is the magic: pause WASM execution and resume later
  emscripten_sleep(0);
}

// User entry - for WASM, we don't need privilege mode switching
// Just call the user program directly
extern "C" void user_entry(void) {
  // Jump to user base address
  typedef void (*user_func_t)(void);
  user_func_t user_main = (user_func_t)current_proc->user_pc;
  user_main();

  // If user program returns, exit the process
  current_proc->state = TERMINATED;
  yield();
}

void yield(void) {
  if (!current_proc || !idle_proc) {
    PANIC("current_proc or idle_proc is null");
  }

  Process *next = process_next_runnable();

  // No runnable process other than the current one
  if (next == current_proc) {
    return;
  }

  // No page table switching needed for WASM
  // Just switch the current process and yield execution
  Process *prev = current_proc;
  current_proc = next;
  switch_context(&prev->stack_ptr, &current_proc->stack_ptr);
}

// Syscall handlers for user programs
extern "C" {

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
  if (!current_proc) {
    return nullptr;
  }

  // For WASM, we don't use page tables, so just allocate physical memory
  // and return it directly as a virtual address
  void *paddr = page_allocate(current_proc->pid, 1);
  if (!paddr) {
    yield();
    return nullptr;
  }

  // In WASM, physical address = virtual address (no MMU)
  // Still track the heap address for consistency
  uintptr_t vaddr = current_proc->heap_next_vaddr;
  current_proc->heap_next_vaddr += PAGE_SIZE;

  yield();
  return paddr; // Return physical address directly
}
}

// Main entry point for WASM
extern "C" void kernel_main(void) {
  oprintf("Otium OS starting on WASM\n");
  kernel_common();
}

// Emscripten calls main, so we just call kernel_main
int main() {
  kernel_main();
  return 0;
}
