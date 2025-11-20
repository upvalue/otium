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

void ou_shutdown(void) {
  oprintf("Shutdown syscall invoked by process %s (pidx=%d, pid=%lu)\n", current_proc->name, current_proc->pidx, current_proc->pid);
  shutdown_all_processes();
  // shutdown_all_processes calls kernel_exit() and never returns
}

void *ou_alloc_page(void) {
  PageAddr result = process_alloc_mapped_page(current_proc, true, true, false);
  return result.as_ptr();
}

PageAddr ou_get_arg_page(void) { return process_get_arg_page(); }

PageAddr ou_get_comm_page(void) { return process_get_comm_page(); }

PageAddr ou_get_storage(void) { return process_get_storage_page(); }

Pid ou_proc_lookup(const char *name) {
  // process_lookup now returns Pid directly
  return process_lookup(StringView(name));
}

int ou_io_puts(const char *str, int size) {
  for (int i = 0; i < size; i++) {
    oputchar(str[i]);
  }
  return 1;
}

IpcResponse ou_ipc_send(Pid target_pid, uintptr_t flags, intptr_t method, intptr_t arg0, intptr_t arg1, intptr_t arg2) {
  // Soft assert: ensure method doesn't overflow into flags field (lower 8 bits should be 0)
  if ((method & 0xFF) != 0) {
    oprintf("WARNING: Method ID %d overflows into flags field\n", method);
  }

  // Pack method and flags into single value
  uintptr_t method_and_flags = IPC_PACK_METHOD_FLAGS(method, flags);

  TRACE_IPC(LLOUD, "IPC send from pidx %d (pid %lu) to pid %lu, method=%d, flags=%x", current_proc->pidx.raw(), current_proc->pid.raw(), target_pid.raw(), method, flags);

  // Look up target by pid
  Pidx target_pidx = process_lookup_by_pid(target_pid);
  if (target_pidx == PIDX_INVALID) {
    TRACE_IPC(LSOFT, "IPC send failed: target pid %lu not found", target_pid.raw());
    IpcResponse resp;
    resp.error_code = IPC__PID_NOT_FOUND;
    resp.values[0] = 0;
    resp.values[1] = 0;
    resp.values[2] = 0;
    return resp;
  }

  Process *target = process_lookup_by_pidx(target_pidx);

  // Handle comm page transfer if requested (send direction)
  if (flags & IPC_FLAG_SEND_COMM_DATA) {
    if (!current_proc->comm_page.is_null() && !target->comm_page.is_null()) {
      TRACE_IPC(LSOFT, "IPC: copying comm page from sender pidx %d to receiver pidx %d", current_proc->pidx.raw(), target_pidx.raw());
      memcpy(target->comm_page.as_ptr(), current_proc->comm_page.as_ptr(), OT_PAGE_SIZE);
    }
  }

  // Set up message
  target->pending_message.sender_pid = current_proc->pid;  // Fill in sender's globally unique PID
  target->pending_message.method_and_flags = method_and_flags;
  target->pending_message.args[0] = arg0;
  target->pending_message.args[1] = arg1;
  target->pending_message.args[2] = arg2;
  target->has_pending_message = true;
  target->blocked_sender = current_proc;

  TRACE_IPC(LLOUD, "IPC: switching to target process pidx %d (pid %lu)", target_pidx.raw(), target_pid.raw());

  // If target is waiting, wake it and switch to it immediately (like RISC-V)
  if (target->state == IPC_WAIT) {
    target->state = RUNNABLE;
    process_switch_to(target); // Direct context switch - receiver will process and reply
    // After this returns, we're back in our own context with response available
  } else {
    TRACE_IPC(LLOUD, "IPC: target not in IPC_WAIT, yielding normally");
    yield();
  }

  // When we get here, receiver has replied and switched back to us
  // Response is in our (sender's) pending_response
  TRACE_IPC(LLOUD, "IPC send returning: error=%d, values=[%d, %d, %d]", current_proc->pending_response.error_code,
            current_proc->pending_response.values[0], current_proc->pending_response.values[1],
            current_proc->pending_response.values[2]);

  return current_proc->pending_response;
}

IpcMessage ou_ipc_recv(void) {
  if (current_proc->has_pending_message) {
    TRACE_IPC(LLOUD, "Process pidx %d (pid %lu) receiving pending message from pid %lu", current_proc->pidx.raw(), current_proc->pid.raw(), current_proc->pending_message.sender_pid.raw());
    IpcMessage msg = current_proc->pending_message;
    current_proc->has_pending_message = false;
    return msg;
  } else {
    TRACE_IPC(LLOUD, "Process pidx %d (pid %lu) entering IPC_WAIT", current_proc->pidx.raw(), current_proc->pid.raw());
    current_proc->state = IPC_WAIT;
    yield();
    // Will resume here when message arrives
    intptr_t method = IPC_UNPACK_METHOD(current_proc->pending_message.method_and_flags);
    uintptr_t flags = IPC_UNPACK_FLAGS(current_proc->pending_message.method_and_flags);
    TRACE_IPC(LLOUD, "Process pidx %d (pid %lu) woken from IPC_WAIT, msg: sender_pid=%lu flags=%x method=%d args=[%d, %d, %d]",
              current_proc->pidx.raw(), current_proc->pid.raw(), current_proc->pending_message.sender_pid.raw(), flags, method,
              current_proc->pending_message.args[0], current_proc->pending_message.args[1],
              current_proc->pending_message.args[2]);
    IpcMessage msg = current_proc->pending_message;
    current_proc->has_pending_message = false;
    return msg;
  }
}

void ou_ipc_reply(IpcResponse response) {
  TRACE_IPC(LLOUD, "Process pidx %d (pid %lu) replying: error=%d, values=[%d, %d, %d]", current_proc->pidx.raw(), current_proc->pid.raw(), response.error_code,
            response.values[0], response.values[1], response.values[2]);

  if (current_proc->blocked_sender) {
    Process *sender = current_proc->blocked_sender;

    // Copy comm page back if response has comm data (receive direction)
    uint8_t request_flags = IPC_UNPACK_FLAGS(current_proc->pending_message.method_and_flags);
    if (request_flags & IPC_FLAG_RECV_COMM_DATA) {
      if (!current_proc->comm_page.is_null() && !sender->comm_page.is_null()) {
        TRACE_IPC(LSOFT, "IPC reply: copying comm page from server pidx %d back to client pidx %d", current_proc->pidx.raw(), sender->pidx.raw());
        memcpy(sender->comm_page.as_ptr(), current_proc->comm_page.as_ptr(), OT_PAGE_SIZE);
      }
    }

    // Store response in SENDER's pending_response field (they will read it)
    sender->pending_response.error_code = response.error_code;
    sender->pending_response.values[0] = response.values[0];
    sender->pending_response.values[1] = response.values[1];
    sender->pending_response.values[2] = response.values[2];

    current_proc->blocked_sender = nullptr;
    TRACE_IPC(LLOUD, "IPC reply sent, immediately switching back to sender pidx %d (pid %lu)", sender->pidx, sender->pid);
    // Switch back to sender immediately (like RISC-V) - receiver will resume when scheduled again
    process_switch_to(sender);
    // After this returns (when we're scheduled again), continue normally
  } else {
    TRACE_IPC(LSOFT, "IPC reply called but no blocked sender");
  }
}