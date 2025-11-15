#ifndef OT_SHARED_ERROR_CODES_HPP
#define OT_SHARED_ERROR_CODES_HPP

#include "ot/common.h"

enum ErrorCode {
  NONE = 0,
  /** Unexpected condition occurred */
  KERNEL__INVARIANT_VIOLATION = 1,
  /** Target process not found */
  IPC__PID_NOT_FOUND = 2,
  /** Method not known by receiver */
  IPC__METHOD_NOT_KNOWN = 3,
};

inline const char *error_code_to_string(ErrorCode code) {
  switch (code) {
  case NONE:
    return "none";
  case KERNEL__INVARIANT_VIOLATION:
    return "kernel-invariant-violation";
  case IPC__PID_NOT_FOUND:
    return "ipc-pid-not-found";
  case IPC__METHOD_NOT_KNOWN:
    return "ipc-method-not-known";
  default:
    return "unknown-error-code";
  }
};

#endif