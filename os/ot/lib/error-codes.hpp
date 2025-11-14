#ifndef OT_SHARED_ERROR_CODES_HPP
#define OT_SHARED_ERROR_CODES_HPP

#include "ot/common.h"

enum ErrorCode {
  NONE = 0,
  /** Unexpected condition occurred */
  KERNEL__INVARIANT_VIOLATION = 1,
  /** Process has not pid */
  KERNEL__IPC_SEND_MESSAGE__PID_NOT_FOUND = 2,
  /** Process has too many messages already */
  KERNEL__IPC_SEND_MESSAGE__OVERFLOW = 3,
  /** Process cannot send message to itself */
  KERNEL__IPC_SEND_MESSAGE__SELF_SEND = 4,
};

inline const char *error_code_to_string(ErrorCode code) {
  switch (code) {
  case KERNEL__IPC_SEND_MESSAGE__PID_NOT_FOUND:
    return "kernel.ipc-send-message.pid-not-found";
  case KERNEL__IPC_SEND_MESSAGE__OVERFLOW:
    return "kernel.ipc-send-message.overflow";
  case KERNEL__IPC_SEND_MESSAGE__SELF_SEND:
    return "kernel.ipc-send-message.self-send";
  default:
    return "unknown-error-code";
  }
};

#endif