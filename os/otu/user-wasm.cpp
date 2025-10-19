// user-wasm.cpp - WASM syscall implementation

#include "otk/kernel.hpp"
#include "otu/user.hpp"

// In WASM, syscalls are just direct function calls
// No need for ecall or trap handling

// For WASM, user programs can directly call kernel functions
// since everything is linked together. We just provide the
// user API functions that call kernel syscall handlers.

extern "C" {

__attribute__((noreturn)) void exit(void) {
  for (;;)
    ;
}

// Forward declarations of kernel syscall handlers
void kernel_syscall_putchar(char ch);
int kernel_syscall_getchar(void);
void kernel_syscall_yield(void);
void kernel_syscall_exit(void);
void *kernel_syscall_alloc_page(void);

// Note: oputchar and ogetchar are defined in platform-wasm.cpp
// and used by both kernel and user code

void ou_yield(void) { kernel_syscall_yield(); }

__attribute__((noreturn)) void ou_exit(void) {
  kernel_syscall_exit();
  // Should never return, but just in case
  while (1)
    ;
}

void *ou_alloc_page(void) { return kernel_syscall_alloc_page(); }

// No special start routine needed for WASM
// The kernel will call shell_main() directly
}
