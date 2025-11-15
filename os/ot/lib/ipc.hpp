#ifndef OT_LIB_IPC_HPP
#define OT_LIB_IPC_HPP

#include "ot/common.h"
#include "ot/lib/error-codes.hpp"

struct IpcMessage {
  intptr_t pid;    // Sender PID (filled by kernel)
  intptr_t method; // Method ID
  intptr_t extra;  // Additional argument
};

struct IpcResponse {
  ErrorCode error_code;
  intptr_t a;
  intptr_t b;
};

#endif
