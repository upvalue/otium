#ifndef OT_LIB_IPC_HPP
#define OT_LIB_IPC_HPP

#include "ot/common.h"
#include "ot/lib/error-codes.hpp"
#include "ot/lib/typed-int.hpp"

struct IpcMessage {
  Pid sender_pid;               // Sender's globally unique PID (filled by kernel)
  uintptr_t method_and_flags;   // Combined: upper bits = method, lower 8 bits = flags
  intptr_t args[3];             // Method-specific arguments (increased from 2 to 3)
};

struct IpcResponse {
  ErrorCode error_code;
  intptr_t values[3];  // Return values
};

// IPC flags (occupy lower 8 bits)
#define IPC_FLAG_NONE 0x00              // No special flags
#define IPC_FLAG_SEND_COMM_DATA 0x01    // Request has data in comm page (copy to server)
#define IPC_FLAG_RECV_COMM_DATA 0x02    // Response will have data in comm page (copy from server)
#define IPC_FLAG_HAS_COMM_DATA IPC_FLAG_SEND_COMM_DATA  // Legacy alias

// Reserved method IDs (below user-defined range starting at 0x1000)
#define IPC_METHOD_SHUTDOWN 0x0100  // Universal shutdown method for all servers

// Helper macros for packing/unpacking method and flags
#define IPC_PACK_METHOD_FLAGS(method, flags) (((uintptr_t)(method)) | ((uintptr_t)(flags)))
#define IPC_UNPACK_METHOD(method_and_flags) ((intptr_t)((method_and_flags) & ~0xFFUL))
#define IPC_UNPACK_FLAGS(method_and_flags) ((uintptr_t)((method_and_flags) & 0xFFUL))

#endif
