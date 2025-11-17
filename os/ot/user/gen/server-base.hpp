#pragma once
#include "ot/lib/ipc.hpp"
#include "ot/user/user.hpp"

// Base class for all generated IPC servers
// Provides common shutdown handling without virtual methods
struct ServerBase {
  // Check if this is a shutdown request and handle it
  // Returns true if shutdown was handled (server should exit)
  bool handle_shutdown_if_requested(const IpcMessage& msg) {
    intptr_t method = IPC_UNPACK_METHOD(msg.method_and_flags);
    if (method == IPC_METHOD_SHUTDOWN) {
      IpcResponse resp = {NONE, {0, 0, 0}};
      ou_ipc_reply(resp);  // Reply with success
      ou_exit();           // Then exit cleanly
      return true;         // Never reached
    }
    return false;
  }
};
