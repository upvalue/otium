// Auto-generated TCL variable registration for IPC method IDs, flags, and error codes
#pragma once
#include "ot/lib/ipc.hpp"
#include "ot/lib/error-codes.hpp"
#include "ot/user/tcl.hpp"

inline void register_ipc_method_vars(tcl::Interp &i) {
  char buf[32];

  // Universal IPC methods
  osnprintf(buf, sizeof(buf), "%d", IPC_METHOD_SHUTDOWN);
  i.set_var("IPC_METHOD_SHUTDOWN", buf);

  // IPC flags
  osnprintf(buf, sizeof(buf), "%d", IPC_FLAG_NONE);
  i.set_var("IPC_FLAG_NONE", buf);

  osnprintf(buf, sizeof(buf), "%d", IPC_FLAG_HAS_COMM_DATA);
  i.set_var("IPC_FLAG_HAS_COMM_DATA", buf);

  // Fibonacci service methods
  osnprintf(buf, sizeof(buf), "%d", 0x1000);
  i.set_var("FIBONACCI_CALC_FIB", buf);
  osnprintf(buf, sizeof(buf), "%d", 0x1100);
  i.set_var("FIBONACCI_CALC_PAIR", buf);
  osnprintf(buf, sizeof(buf), "%d", 0x1200);
  i.set_var("FIBONACCI_GET_CACHE_SIZE", buf);
  // Graphics service methods
  osnprintf(buf, sizeof(buf), "%d", 0x1300);
  i.set_var("GRAPHICS_GET_FRAMEBUFFER", buf);
  osnprintf(buf, sizeof(buf), "%d", 0x1400);
  i.set_var("GRAPHICS_FLUSH", buf);

  // Error codes
  osnprintf(buf, sizeof(buf), "%d", ErrorCode::NONE);
  i.set_var("ERROR_NONE", buf);

  osnprintf(buf, sizeof(buf), "%d", ErrorCode::FIBONACCI__INVALID_INPUT);
  i.set_var("ERROR_FIBONACCI__INVALID_INPUT", buf);
  osnprintf(buf, sizeof(buf), "%d", ErrorCode::GRAPHICS__NOT_INITIALIZED);
  i.set_var("ERROR_GRAPHICS__NOT_INITIALIZED", buf);
}
