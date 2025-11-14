// user-wasm.cpp - WASM syscall implementation

#include "ot/core/kernel.hpp"
#include "ot/platform/user.hpp"

// In WASM, syscalls are just direct function calls
// No need for ecall or trap handling

// For WASM, user programs can directly call kernel functions
// since everything is linked together. We just provide the
// user API functions that call kernel syscall handlers.

extern "C" {

__attribute__((noreturn)) void exit(int) {
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
  // kernel_syscall_exit();
  process_exit(current_proc);
  ou_yield();
}

void *ou_alloc_page(void) { return kernel_syscall_alloc_page(); }

// No special start routine needed for WASM
// The kernel will call shell_main() directly
}

PageAddr kernel_syscall_get_arg_page(void);
PageAddr ou_get_arg_page(void) { return kernel_syscall_get_arg_page(); }

PageAddr kernel_syscall_get_msg_page(int msg_idx);
PageAddr ou_get_msg_page(int msg_idx) {
  return kernel_syscall_get_msg_page(msg_idx);
}
PageAddr kernel_syscall_get_comm_page(void);
PageAddr ou_get_comm_page(void) { return kernel_syscall_get_comm_page(); }

int kernel_syscall_ipc_check_message(void);
int kernel_syscall_ipc_send_message(int pid);
int kernel_syscall_proc_lookup(const char *name);
int kernel_syscall_ipc_pop_message(void);

int ou_proc_lookup(const char *name) {
  return kernel_syscall_proc_lookup(name);
}

int kernel_syscall_io_puts(const char *str, int size);

int ou_io_puts(const char *str, int size) {
  return kernel_syscall_io_puts(str, size);
}

int ou_ipc_check_message(void) { return kernel_syscall_ipc_check_message(); }

int ou_ipc_send_message(int pid) {
  return kernel_syscall_ipc_send_message(pid);
}

int ou_ipc_pop_message(void) { return kernel_syscall_ipc_pop_message(); }