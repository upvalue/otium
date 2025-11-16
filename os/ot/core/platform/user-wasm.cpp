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
}

void ou_yield(void) { yield(); }

__attribute__((noreturn)) void ou_exit(void) {
  current_proc->state = TERMINATED;
  // process_exit(current_proc);
  yield();
  // Should never reach here, but loop to satisfy noreturn attribute
  for (;;)
    ;
}

void *ou_alloc_page(void) {
  PageAddr result = process_alloc_mapped_page(current_proc, true, true, false);
  return result.as_ptr();
}

PageAddr ou_get_arg_page(void) { return process_get_arg_page(); }

PageAddr ou_get_comm_page(void) { return process_get_comm_page(); }

PageAddr ou_get_storage(void) { return process_get_storage_page(); }

int ou_proc_lookup(const char *name) {
  Process *proc = process_lookup(name);
  return proc ? proc->pid : 0;
}

int ou_io_puts(const char *str, int size) {
  for (int i = 0; i < size; i++) {
    oputchar(str[i]);
  }
  return 1;
}

IpcResponse ou_ipc_send(int pid, intptr_t method, intptr_t extra) {
  TRACE_IPC(LSOFT, "IPC send from %d to %d, method=%d", current_proc->pid, pid, method);

  Process *target = process_lookup(pid);
  if (!target) {
    TRACE_IPC(LSOFT, "IPC send failed: target PID %d not found", pid);
    IpcResponse resp;
    resp.error_code = IPC__PID_NOT_FOUND;
    resp.a = 0;
    resp.b = 0;
    return resp;
  }

  // Set up message
  target->pending_message.pid = current_proc->pid;
  target->pending_message.method = method;
  target->pending_message.extra = extra;
  target->has_pending_message = true;
  target->blocked_sender = current_proc;

  TRACE_IPC(LLOUD, "IPC: switching to target process %d", pid);

  // If target is waiting, wake it and switch to it immediately (like RISC-V)
  if (target->state == IPC_WAIT) {
    target->state = RUNNABLE;
    process_switch_to(target);  // Direct context switch - receiver will process and reply
    // After this returns, we're back in our own context with response available
  } else {
    TRACE_IPC(LLOUD, "IPC: target not in IPC_WAIT, yielding normally");
    yield();
  }

  // When we get here, receiver has replied and switched back to us
  // Response is in our (sender's) pending_response
  TRACE_IPC(LLOUD, "IPC send returning: error=%d, a=%d, b=%d", current_proc->pending_response.error_code,
            current_proc->pending_response.a, current_proc->pending_response.b);

  return current_proc->pending_response;
}

IpcMessage ou_ipc_recv(void) {
  if (current_proc->has_pending_message) {
    TRACE_IPC(LLOUD, "Process %d receiving pending message", current_proc->pid);
    IpcMessage msg = current_proc->pending_message;
    current_proc->has_pending_message = false;
    return msg;
  } else {
    TRACE_IPC(LSOFT, "Process %d entering IPC_WAIT", current_proc->pid);
    current_proc->state = IPC_WAIT;
    yield();
    // Will resume here when message arrives
    TRACE_IPC(LLOUD, "Process %d woken from IPC_WAIT, msg: pid=%d method=%d extra=%d", current_proc->pid,
              current_proc->pending_message.pid, current_proc->pending_message.method,
              current_proc->pending_message.extra);
    IpcMessage msg = current_proc->pending_message;
    current_proc->has_pending_message = false;
    return msg;
  }
}

void ou_ipc_reply(IpcResponse response) {
  TRACE_IPC(LLOUD, "Process %d replying: error=%d, a=%d, b=%d", current_proc->pid, response.error_code, response.a,
            response.b);

  if (current_proc->blocked_sender) {
    Process *sender = current_proc->blocked_sender;

    // Store response in SENDER's pending_response field (they will read it)
    sender->pending_response.error_code = response.error_code;
    sender->pending_response.a = response.a;
    sender->pending_response.b = response.b;

    current_proc->blocked_sender = nullptr;
    TRACE_IPC(LLOUD, "IPC reply sent, immediately switching back to sender %d", sender->pid);
    // Switch back to sender immediately (like RISC-V) - receiver will resume when scheduled again
    process_switch_to(sender);
    // After this returns (when we're scheduled again), continue normally
  } else {
    TRACE_IPC(LSOFT, "IPC reply called but no blocked sender");
  }
}