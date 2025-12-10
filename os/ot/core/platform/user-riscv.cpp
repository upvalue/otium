#include "ot/lib/address.hpp"
#include "ot/lib/messages.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/user.hpp"

extern char __stack_top[];

__attribute__((noreturn)) extern "C" void exit(int status) {
  (void)status; // Bare-metal: ignore status, just spin
  for (;;)
    ;
}

// Stubs required by picolibc
__attribute__((noreturn)) extern "C" void _exit(int status) {
  (void)status;
  for (;;)
    ;
}

// Stub sbrk for malloc - we use TLSF allocator instead, but picolibc needs this
extern "C" void *sbrk(intptr_t increment) {
  (void)increment;
  return (void *)-1; // Always fail - we don't use this
}

// Dummy stderr for assert (picolibc declares as FILE *const)
#include <stdio.h>
static FILE _stderr_file;
FILE *const stderr = &_stderr_file;

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

void ou_exit(void) { syscall(OU_EXIT, 0, 0, 0); }
void ou_yield(void) { syscall(OU_YIELD, 0, 0, 0); }
void ou_shutdown(void) { syscall(OU_SHUTDOWN, 0, 0, 0); }
void *ou_alloc_pages(size_t count) { return (void *)syscall(OU_ALLOC_PAGE, (int)count, 0, 0).a0; }

void *ou_lock_known_memory(KnownMemory km, size_t page_count) {
  return (void *)syscall(OU_LOCK_KNOWN_MEMORY, (int)km, (int)page_count, 0).a0;
}

PageAddr ou_get_sys_page(int type, int msg_idx) { return PageAddr(syscall(OU_GET_SYS_PAGE, type, msg_idx, 0).a0); }

PageAddr ou_get_arg_page(void) { return ou_get_sys_page(OU_SYS_PAGE_ARG, 0); }
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

Pid ou_proc_lookup(const char *name) {
  PageAddr comm_page = ou_get_comm_page();
  if (comm_page.is_null()) {
    return PID_NONE;
  }
  MPackWriter writer(comm_page.as<char>(), OT_PAGE_SIZE);
  writer.str(name);
  return Pid(syscall(OU_PROC_LOOKUP, 0, 0, 0).a0);
}

bool ou_proc_is_alive(Pid pid) { return syscall(OU_PROC_IS_ALIVE, (int)pid.raw(), 0, 0).a0 != 0; }

Pid ou_proc_spawn(const char *name, int argc, char **argv) {
  PageAddr comm_page = ou_get_comm_page();
  if (comm_page.is_null()) {
    return PID_NONE;
  }
  // Write spawn request to comm page: {"name": "...", "args": [...]}
  MPackWriter writer(comm_page.as<char>(), OT_PAGE_SIZE);
  writer.map(2);
  writer.str("name");
  writer.str(name);
  writer.str("args");
  writer.stringarray(argc, argv);
  return Pid(syscall(OU_PROC_SPAWN, 0, 0, 0).a0);
}

IpcResponse ou_ipc_send(Pid target_pid, uintptr_t flags, intptr_t method, intptr_t arg0, intptr_t arg1, intptr_t arg2) {
  // Soft assert: ensure method doesn't overflow into flags field (lower 8 bits should be 0)
  if ((method & 0xFF) != 0) {
    oprintf("WARNING: Method ID %d overflows into flags field\n", method);
  }

  // oprintf("OU_IPC_SEND: sending to pid %lu, method %d, flags %x\n", target_pid.raw(), method, flags);

  // Pack method and flags into single value
  uintptr_t method_and_flags = IPC_PACK_METHOD_FLAGS(method, flags);

  // RISC-V: a0=target_pid, a1=method_and_flags, a2=arg0, a3=syscall_num
  // Additional args: a4=arg1, a5=arg2
  register int a0 __asm__("a0") = target_pid.raw();
  register int a1 __asm__("a1") = method_and_flags;
  register int a2 __asm__("a2") = arg0;
  register int a3 __asm__("a3") = OU_IPC_SEND;
  register int a4 __asm__("a4") = arg1;
  register int a5 __asm__("a5") = arg2;
  register int a6 __asm__("a6") = 0;
  register int a7 __asm__("a7") = 0;

  __asm__ __volatile__("ecall"
                       : "=r"(a0), "=r"(a1), "=r"(a2), "=r"(a4)
                       : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                       : "memory");

  IpcResponse resp;
  resp.error_code = (ErrorCode)a0;
  resp.values[0] = a1;
  resp.values[1] = a2;
  resp.values[2] = a4;
  return resp;
}

IpcMessage ou_ipc_recv(void) {
  // RISC-V: Returns sender_pid, method_and_flags in a0-a1, args in a2, a4-a5
  register int a0 __asm__("a0") = 0;
  register int a1 __asm__("a1") = 0;
  register int a2 __asm__("a2") = 0;
  register int a3 __asm__("a3") = OU_IPC_RECV;
  register int a4 __asm__("a4") = 0;
  register int a5 __asm__("a5") = 0;
  register int a6 __asm__("a6") = 0;
  register int a7 __asm__("a7") = 0;

  __asm__ __volatile__("ecall"
                       : "=r"(a0), "=r"(a1), "=r"(a2), "=r"(a4), "=r"(a5)
                       : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                       : "memory");

  IpcMessage msg;
  msg.sender_pid = Pid(a0);
  msg.method_and_flags = a1;
  msg.args[0] = a2;
  msg.args[1] = a4;
  msg.args[2] = a5;
  return msg;
}

void ou_ipc_reply(IpcResponse response) {
  // RISC-V: a0=error_code, a1-a2=values[0-1], a4=values[2]
  register int a0 __asm__("a0") = response.error_code;
  register int a1 __asm__("a1") = response.values[0];
  register int a2 __asm__("a2") = response.values[1];
  register int a3 __asm__("a3") = OU_IPC_REPLY;
  register int a4 __asm__("a4") = response.values[2];
  register int a5 __asm__("a5") = 0;
  register int a6 __asm__("a6") = 0;
  register int a7 __asm__("a7") = 0;

  __asm__ __volatile__("ecall"
                       : "=r"(a0), "=r"(a1), "=r"(a2)
                       : "r"(a0), "r"(a1), "r"(a2), "r"(a3), "r"(a4), "r"(a5), "r"(a6), "r"(a7)
                       : "memory");
}