// Auto-generated TCL variable registration for IPC method IDs, flags, and error codes
#pragma once
#include <stdio.h>
#include "ot/lib/ipc.hpp"
#include "ot/lib/error-codes.hpp"
#include "ot/user/tcl.hpp"

inline void register_ipc_method_vars(tcl::Interp &i) {
  char buf[32];

  // Universal IPC methods
  snprintf(buf, sizeof(buf), "%d", IPC_METHOD_SHUTDOWN);
  i.set_var("IPC_METHOD_SHUTDOWN", buf);

  // IPC flags
  snprintf(buf, sizeof(buf), "%d", IPC_FLAG_NONE);
  i.set_var("IPC_FLAG_NONE", buf);

  snprintf(buf, sizeof(buf), "%d", IPC_FLAG_HAS_COMM_DATA);
  i.set_var("IPC_FLAG_HAS_COMM_DATA", buf);

  // Fibonacci service methods
  snprintf(buf, sizeof(buf), "%d", 0x1000);
  i.set_var("FIBONACCI_CALC_FIB", buf);
  snprintf(buf, sizeof(buf), "%d", 0x1100);
  i.set_var("FIBONACCI_CALC_PAIR", buf);
  snprintf(buf, sizeof(buf), "%d", 0x1200);
  i.set_var("FIBONACCI_GET_CACHE_SIZE", buf);
  // Graphics service methods
  snprintf(buf, sizeof(buf), "%d", 0x1300);
  i.set_var("GRAPHICS_GET_FRAMEBUFFER", buf);
  snprintf(buf, sizeof(buf), "%d", 0x1400);
  i.set_var("GRAPHICS_FLUSH", buf);
  snprintf(buf, sizeof(buf), "%d", 0x1500);
  i.set_var("GRAPHICS_REGISTER_APP", buf);
  snprintf(buf, sizeof(buf), "%d", 0x1600);
  i.set_var("GRAPHICS_SHOULD_RENDER", buf);
  // Filesystem service methods
  snprintf(buf, sizeof(buf), "%d", 0x1700);
  i.set_var("FILESYSTEM_OPEN", buf);
  snprintf(buf, sizeof(buf), "%d", 0x1800);
  i.set_var("FILESYSTEM_READ", buf);
  snprintf(buf, sizeof(buf), "%d", 0x1900);
  i.set_var("FILESYSTEM_WRITE", buf);
  snprintf(buf, sizeof(buf), "%d", 0x1a00);
  i.set_var("FILESYSTEM_CLOSE", buf);
  snprintf(buf, sizeof(buf), "%d", 0x1b00);
  i.set_var("FILESYSTEM_CREATE_FILE", buf);
  snprintf(buf, sizeof(buf), "%d", 0x1c00);
  i.set_var("FILESYSTEM_CREATE_DIR", buf);
  snprintf(buf, sizeof(buf), "%d", 0x1d00);
  i.set_var("FILESYSTEM_DELETE_FILE", buf);
  snprintf(buf, sizeof(buf), "%d", 0x1e00);
  i.set_var("FILESYSTEM_DELETE_DIR", buf);
  // Keyboard service methods
  snprintf(buf, sizeof(buf), "%d", 0x1f00);
  i.set_var("KEYBOARD_POLL_KEY", buf);

  // Error codes
  snprintf(buf, sizeof(buf), "%d", ErrorCode::NONE);
  i.set_var("ERROR_NONE", buf);

  snprintf(buf, sizeof(buf), "%d", ErrorCode::FIBONACCI__INVALID_INPUT);
  i.set_var("ERROR_FIBONACCI__INVALID_INPUT", buf);
  snprintf(buf, sizeof(buf), "%d", ErrorCode::GRAPHICS__NOT_INITIALIZED);
  i.set_var("ERROR_GRAPHICS__NOT_INITIALIZED", buf);
  snprintf(buf, sizeof(buf), "%d", ErrorCode::GRAPHICS__TOO_MANY_APPS);
  i.set_var("ERROR_GRAPHICS__TOO_MANY_APPS", buf);
  snprintf(buf, sizeof(buf), "%d", ErrorCode::GRAPHICS__NOT_REGISTERED);
  i.set_var("ERROR_GRAPHICS__NOT_REGISTERED", buf);
  snprintf(buf, sizeof(buf), "%d", ErrorCode::FILESYSTEM__FILE_NOT_FOUND);
  i.set_var("ERROR_FILESYSTEM__FILE_NOT_FOUND", buf);
  snprintf(buf, sizeof(buf), "%d", ErrorCode::FILESYSTEM__PATH_TOO_LONG);
  i.set_var("ERROR_FILESYSTEM__PATH_TOO_LONG", buf);
  snprintf(buf, sizeof(buf), "%d", ErrorCode::FILESYSTEM__TOO_MANY_OPEN_FILES);
  i.set_var("ERROR_FILESYSTEM__TOO_MANY_OPEN_FILES", buf);
  snprintf(buf, sizeof(buf), "%d", ErrorCode::FILESYSTEM__INVALID_HANDLE);
  i.set_var("ERROR_FILESYSTEM__INVALID_HANDLE", buf);
  snprintf(buf, sizeof(buf), "%d", ErrorCode::FILESYSTEM__IO_ERROR);
  i.set_var("ERROR_FILESYSTEM__IO_ERROR", buf);
  snprintf(buf, sizeof(buf), "%d", ErrorCode::FILESYSTEM__ALREADY_EXISTS);
  i.set_var("ERROR_FILESYSTEM__ALREADY_EXISTS", buf);
  snprintf(buf, sizeof(buf), "%d", ErrorCode::FILESYSTEM__PARENT_NOT_FOUND);
  i.set_var("ERROR_FILESYSTEM__PARENT_NOT_FOUND", buf);
  snprintf(buf, sizeof(buf), "%d", ErrorCode::FILESYSTEM__DIR_NOT_FOUND);
  i.set_var("ERROR_FILESYSTEM__DIR_NOT_FOUND", buf);
  snprintf(buf, sizeof(buf), "%d", ErrorCode::FILESYSTEM__NOT_EMPTY);
  i.set_var("ERROR_FILESYSTEM__NOT_EMPTY", buf);
  snprintf(buf, sizeof(buf), "%d", ErrorCode::KEYBOARD__NOT_INITIALIZED);
  i.set_var("ERROR_KEYBOARD__NOT_INITIALIZED", buf);
}
