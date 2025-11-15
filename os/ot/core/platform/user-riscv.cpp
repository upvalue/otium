#include "ot/lib/address.hpp"
#include "ot/lib/messages.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/user.hpp"

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
  register int a4 __asm__("a4") = 0;
  register int a5 __asm__("a5") = 0;
  register int a6 __asm__("a6") = 0;
  register int a7 __asm__("a7") = 0;

  __asm__ __volatile__("ecall"
                       : "=r"(a0), "=r"(a1), "=r"(a2)
                       : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
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
  oprintf("ou_get_sys_page: calling syscall %d %d %d\n", type, msg_idx);
  return PageAddr(syscall(OU_GET_SYS_PAGE, type, msg_idx, 0).a0);
}

PageAddr ou_get_arg_page(void) {
  oprintf("ou_get_arg_page: calling syscall %d\n", OU_SYS_PAGE_ARG);
  return ou_get_sys_page(OU_SYS_PAGE_ARG, 0);
}
PageAddr ou_get_comm_page(void) { return ou_get_sys_page(OU_SYS_PAGE_COMM, 0); }
PageAddr ou_get_storage(void) { return ou_get_sys_page(OU_SYS_PAGE_STORAGE, 0); }

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

extern "C" {

IpcResponse ou_ipc_send(int pid, intptr_t method, intptr_t extra) {
  SyscallResult result = syscall(OU_IPC_SEND, pid, method, extra);
  IpcResponse resp;
  resp.error_code = (ErrorCode)result.a0;
  resp.a = result.a1;
  resp.b = result.a2;
  return resp;
}

IpcMessage ou_ipc_recv(void) {
  SyscallResult result = syscall(OU_IPC_RECV, 0, 0, 0);
  IpcMessage msg;
  msg.pid = result.a0;
  msg.method = result.a1;
  msg.extra = result.a2;
  return msg;
}

void ou_ipc_reply(IpcResponse response) {
  syscall(OU_IPC_REPLY, response.error_code, response.a, response.b);
}

} // extern "C"