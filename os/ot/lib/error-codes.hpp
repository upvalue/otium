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
  /** Receiver's IPC wait queue is full */
  IPC__QUEUE_FULL = 5,

  VIRTIO__DEVICE_NOT_FOUND = 6,
  VIRTIO__SETUP_FAIL = 7,

  // Disk errors
  DISK__OUT_OF_BOUNDS = 8,
  DISK__IO_ERROR = 9,
  DISK__DEVICE_ERROR = 10,

  // App framework errors (fonts, graphics, etc.)
  APP__FONT_NOT_LOADED = 11,
  APP__FONT_LOAD_FAILED = 12,
  APP__GLYPH_LOOKUP_FAILED = 13,
  APP__GLYPH_METRICS_FAILED = 14,
  APP__GLYPH_RENDER_FAILED = 15,
  APP__MEMORY_ALLOC_FAILED = 16,

// Generated service error codes (starting at 100)
#include "ot/user/gen/error-codes-gen.hpp"
};

inline const char *error_code_to_string(ErrorCode code) {
  switch (code) {
  case NONE:
    return "none";
  case KERNEL__INVARIANT_VIOLATION:
    return "kernel.invariant-violation";
  case IPC__PID_NOT_FOUND:
    return "ipc.pid-not-found";
  case IPC__METHOD_NOT_KNOWN:
    return "ipc.method-not-known";
  case IPC__METHOD_NOT_IMPLEMENTED:
    return "ipc.method-not-implemented";
  case IPC__QUEUE_FULL:
    return "ipc.queue-full";
  case VIRTIO__DEVICE_NOT_FOUND:
    return "virtio.device-not-found";
  case VIRTIO__SETUP_FAIL:
    return "virtio.setup-fail";
  case DISK__OUT_OF_BOUNDS:
    return "disk.out-of-bounds";
  case DISK__IO_ERROR:
    return "disk.io-error";
  case DISK__DEVICE_ERROR:
    return "disk.device-error";
  case APP__FONT_NOT_LOADED:
    return "app.font-not-loaded";
  case APP__FONT_LOAD_FAILED:
    return "app.font-load-failed";
  case APP__GLYPH_LOOKUP_FAILED:
    return "app.glyph-lookup-failed";
  case APP__GLYPH_METRICS_FAILED:
    return "app.glyph-metrics-failed";
  case APP__GLYPH_RENDER_FAILED:
    return "app.glyph-render-failed";
  case APP__MEMORY_ALLOC_FAILED:
    return "app.memory-alloc-failed";

// Generated service error code cases
#include "ot/user/gen/error-codes-gen-switch.hpp"

  default:
    return "unknown-error-code";
  }
};

#endif