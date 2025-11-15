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

IpcResponse ou_ipc_send(int, intptr_t, intptr_t) {
  PANIC("IPC not implemented for WASM");
}

IpcMessage ou_ipc_recv(void) {
  PANIC("IPC not implemented for WASM");
}

void ou_ipc_reply(IpcResponse) {
  PANIC("IPC not implemented for WASM");
}