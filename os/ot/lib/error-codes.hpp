#ifndef OT_SHARED_ERROR_CODES_HPP
#define OT_SHARED_ERROR_CODES_HPP

#include "ot/common.h"

enum ErrorCode {
  NONE = 0,
  /** Unexpected condition occurred */
  KERNEL__INVARIANT_VIOLATION = 1,
};

inline const char *error_code_to_string(ErrorCode code) {
  switch (code) {
  default:
    return "unknown-error-code";
  }
};

#endif