#include "ot/common.h"
#include "ot/kernel/kernel.hpp"
#include "ot/shared/messages.hpp"

bool ipc_send_message(Process *sender, int target_pid) {
  PageAddr comm_page = sender->comm_page.first;
  OT_SOFT_ASSERT("ipc_send_message: sender comm page is null",
                 !comm_page.is_null());
  if (comm_page.is_null()) {
    return false;
  }

  Process *receiver = process_lookup(target_pid);
  if (!receiver) {
    MsgError error(comm_page);
    osnprintf(ot_scratch_buffer, OT_PAGE_SIZE,
              "ipc_send_message: pid %d not found to receive message from "
              "process %d %s",
              target_pid, sender->pid, sender->name);
    error.serialize(KERNEL__IPC_SEND_MESSAGE__PID_NOT_FOUND, ot_scratch_buffer);
    return false;
  }

  return true;
}