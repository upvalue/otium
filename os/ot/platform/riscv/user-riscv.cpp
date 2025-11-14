#include "ot/lib/address.hpp"
#include "ot/lib/messages.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/platform/user.hpp"

extern char __stack_top[];

__attribute__((noreturn)) extern "C" void exit(void) {
  for (;;)
    ;
}

struct SyscallResult {
  int a0, a1, a2;
};

SyscallResult syscall(int sysno, int arg0, int arg1, int arg2) {
  register int a0 __asm__("a0") = arg0;
  register int a1 __asm__("a1") = arg1;
  register int a2 __asm__("a2") = arg2;
  register int a3 __asm__("a3") = sysno;

  __asm__ __volatile__("ecall"
                       : "=r"(a0)
                       : "r"(a0), "r"(a1), "r"(a2), "r"(a3)
                       : "memory");

  SyscallResult result = {.a0 = a0, .a1 = a1, .a2 = a2};

  return result;
}

// Syscall wrappers for kernel services
// Note: I/O functions (oputchar/ogetchar) are handled by forwarding SBI calls in trap handler

extern "C" {

void ou_exit(void) { syscall(OU_EXIT, 0, 0, 0); }
void ou_yield(void) { syscall(OU_YIELD, 0, 0, 0); }
void *ou_alloc_page(void) { return (void *)syscall(OU_ALLOC_PAGE, 0, 0, 0).a0; }

} // extern "C"

PageAddr ou_get_sys_page(int type, int msg_idx) {
  return PageAddr(syscall(OU_GET_SYS_PAGE, type, msg_idx, 0).a0);
}

PageAddr ou_get_msg_page(int msg_idx) {
  return ou_get_sys_page(OU_SYS_PAGE_MSG, msg_idx);
}
PageAddr ou_get_arg_page(void) { return ou_get_sys_page(OU_SYS_PAGE_ARG, 0); }
PageAddr ou_get_comm_page(void) { return ou_get_sys_page(OU_SYS_PAGE_COMM, 0); }

int ou_io_puts(const char *str, int size) {
  PageAddr comm_page = ou_get_comm_page();
  if (comm_page.is_null()) {
    return 0;
  }
  MsgString msg(comm_page.as<char>(), OT_PAGE_SIZE);
  StringView sv(str, size);
  MsgSerializationError error = msg.serialize(sv);
  if (error != MSG_SERIALIZATION_OK) {
    return 0;
  }
  return syscall(OU_IO_PUTS, 0, 0, 0).a0;
}

int ou_proc_lookup(const char *name) {
  PageAddr comm_page = ou_get_comm_page();
  if (comm_page.is_null()) {
    return 0;
  }
  MPackWriter writer(comm_page.as<char>(), OT_PAGE_SIZE);
  writer.str(name);
  return syscall(OU_PROC_LOOKUP, 0, 0, 0).a0;
}

int ou_ipc_check_message(void) {
  return syscall(OU_IPC_CHECK_MESSAGE, 0, 0, 0).a0;
}

int ou_ipc_send_message(int pid) {
  return syscall(OU_IPC_SEND_MESSAGE, pid, 0, 0).a0;
}

int ou_ipc_pop_message(void) { return syscall(OU_IPC_POP_MESSAGE, 0, 0, 0).a0; }