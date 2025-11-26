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
  /** Method known but not implemented */
  IPC__METHOD_NOT_IMPLEMENTED = 4,

// Generated service error codes (starting at 100)
#include "ot/user/gen/error-codes-gen.hpp"
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
  case IPC__METHOD_NOT_IMPLEMENTED:
    return "ipc-method-not-implemented";

// Generated service error code cases
#include "ot/user/gen/error-codes-gen-switch.hpp"

  default:
    return "unknown-error-code";
  }
};

#endif