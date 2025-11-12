#include "ot/common.h"
#include "ot/kernel/kernel.hpp"
#include "ot/shared/error-codes.hpp"
#include "ot/shared/messages.hpp"

bool ipc_send_message(Process *sender, int target_pid) {
  PageAddr comm_page = sender->comm_page.first;
  OT_SOFT_ASSERT("ipc_send_message: sender comm page is null", !comm_page.is_null());
  if (comm_page.is_null()) {
    return false;
  }

  MsgError error(comm_page);

  Process *receiver = process_lookup(target_pid);
  if (!receiver) {
    error.serialize(KERNEL__IPC_SEND_MESSAGE__PID_NOT_FOUND, "pid %d not found to receive message (sender %d %s)",
                    target_pid, sender->pid, sender->name);
    return false;
  }

  if (receiver->pid == sender->pid) {
    error.serialize(KERNEL__IPC_SEND_MESSAGE__SELF_SEND, "process cannot send message to itself (sender %d %s)",
                    sender->pid, sender->name);
    return false;
  }

  if (receiver->msg_count >= OT_MSG_LIMIT) {
    error.serialize(KERNEL__IPC_SEND_MESSAGE__OVERFLOW,
                    "receiver process %d has too many messages already (sender %d %s)", receiver->pid, sender->pid,
                    sender->name);
    return false;
  }

  int msg_idx = receiver->msg_count;

  // Allocate page for message if necessary
  TRACE_IPC(LSOFT, "allocating page for message %d to %d (sender %d %s)", msg_idx, receiver->pid, sender->pid,
            sender->name);

  PageAddr msg_page = receiver->msg_pages[msg_idx].first;

  if (msg_page.is_null()) {
    receiver->msg_pages[msg_idx] = process_alloc_mapped_page(receiver, true, true, false);

    msg_page = receiver->msg_pages[msg_idx].first;

    if (msg_page.is_null()) {
      error.serialize(KERNEL__INVARIANT_VIOLATION, "failed to allocate message page %d (receiver %d %s), sender %d %s",
                      msg_idx, receiver->pid, receiver->name, sender->pid, sender->name);
      OT_SOFT_ASSERT("failed to allocate message page", !msg_page.is_null());
      return false;
    }
  }

  OT_SOFT_ASSERT("ipc_send_message: msg page is null", !msg_page.is_null());

  memcpy(msg_page.as_ptr(), comm_page.as_ptr(), OT_PAGE_SIZE);

  // Set the sender PID for this message
  receiver->msg_send_pid[msg_idx] = sender->pid;

  receiver->msg_count++;

  TRACE_IPC(LSOFT, "sent message %d to (receiver %d %s) (sender %d %s)", msg_idx, receiver->pid, receiver->name,
            sender->pid, sender->name);

  return true;
}

int ipc_pop_message(Process *receiver) {
  if (receiver->msg_count == 0) {
    return 0;
  }

  int msg_idx = --receiver->msg_count;

  // Clear the sender PID for this message
  receiver->msg_send_pid[msg_idx] = 0;

  return msg_idx;
}