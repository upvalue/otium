// user-wasm.cpp - WASM syscall implementation

#include "ot/core/kernel.hpp"
#include "ot/user/user.hpp"

// In WASM, syscalls are just direct function calls
// No need for ecall or trap handling

// For WASM, user programs can directly call kernel functions
// since everything is linked together.

extern "C" {

__attribute__((noreturn)) void exit(int) {
  for (;;)
    ;
}

// Note: oputchar is defined in platform-wasm.cpp
extern int oputchar(char ch);

void ou_yield(void) { yield(); }

__attribute__((noreturn)) void ou_exit(void) {
  process_exit(current_proc);
  yield();
}

void *ou_alloc_page(void) {
  PageAddr result = process_alloc_mapped_page(current_proc, true, true, false);
  yield();
  return result.as_ptr();
}

// No special start routine needed for WASM
// The kernel will call shell_main() directly
}

PageAddr ou_get_arg_page(void) { return process_get_arg_page(); }

PageAddr ou_get_msg_page(int msg_idx) { return process_get_msg_page(msg_idx); }

PageAddr ou_get_comm_page(void) { return process_get_comm_page(); }

PageAddr ou_get_storage(void) { return process_get_storage_page(); }

int ou_proc_lookup(const char *name) {
  Process *proc = process_lookup(name);
  yield();
  return proc ? proc->pid : 0;
}

int ou_io_puts(const char *str, int size) {
  for (int i = 0; i < size; i++) {
    oputchar(str[i]);
  }
  yield();
  return 1;
}

int ou_ipc_check_message(void) { return current_proc->msg_count; }

int ou_ipc_send_message(int pid) { return ipc_send_message(current_proc, pid); }

int ou_ipc_pop_message(void) { return ipc_pop_message(current_proc); }