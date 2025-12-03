#include "ot/config.h"
#include "ot/lib/logger.hpp"
#include "ot/user/gen/keyboard-server.hpp"
#include "ot/user/keyboard/backend.hpp"

#if OT_KEYBOARD_BACKEND == OT_KEYBOARD_BACKEND_NONE
#include "ot/user/keyboard/backend-none.hpp"
#elif OT_KEYBOARD_BACKEND == OT_KEYBOARD_BACKEND_VIRTIO
#include "ot/user/keyboard/backend-virtio.hpp"
#endif

// Keyboard server implementation with instance state
struct KeyboardServerImpl : KeyboardServerBase {
  KeyboardBackend *backend;
  Logger l;

  KeyboardServerImpl() : backend(nullptr), l("kbd") {}

  Result<PollKeyResult, ErrorCode> handle_poll_key() override {
    if (!backend) {
      return Result<PollKeyResult, ErrorCode>::err(KEYBOARD__NOT_INITIALIZED);
    }

    KeyEvent event;
    PollKeyResult result;

    if (backend->poll_key(&event)) {
      // Key event available
      result.has_key = 1;
      result.code = event.code;
      result.flags = event.flags;
    } else {
      // No key available
      result.has_key = 0;
      result.code = 0;
      result.flags = 0;
    }

    return Result<PollKeyResult, ErrorCode>::ok(result);
  }
};

void proc_keyboard(void) {
  Logger l("kbd");
  l.log("Keyboard driver starting...");

// Select and initialize backend based on compile-time configuration
#if OT_KEYBOARD_BACKEND == OT_KEYBOARD_BACKEND_NONE
  l.log("Using none keyboard backend (no input)");
  static char backend_buffer[sizeof(NoneKeyboardBackend)] __attribute__((aligned(alignof(NoneKeyboardBackend))));
  NoneKeyboardBackend *backend_ptr = new (backend_buffer) NoneKeyboardBackend();
  NoneKeyboardBackend &backend = *backend_ptr;
#elif OT_KEYBOARD_BACKEND == OT_KEYBOARD_BACKEND_VIRTIO
  l.log("Using VirtIO keyboard backend");
  static char backend_buffer[sizeof(VirtioKeyboardBackend)] __attribute__((aligned(alignof(VirtioKeyboardBackend))));
  VirtioKeyboardBackend *backend_ptr = nullptr;

  Result<uintptr_t, ErrorCode> result = VirtIODevice::scan_for_device(VIRTIO_ID_INPUT);
  if (result.is_err()) {
    l.log("ERROR: No VirtIO input device found!");
    ou_exit();
  }

  backend_ptr = new (backend_buffer) VirtioKeyboardBackend(result.value());

  if (!backend_ptr) {
    l.log("ERROR: Failed to create VirtIO keyboard backend!");
    ou_exit();
  }

  VirtioKeyboardBackend &backend = *backend_ptr;
#else
#error "Unknown keyboard backend"
#endif

  if (!backend.init()) {
    l.log("ERROR: Failed to initialize keyboard backend");
    ou_exit();
  }

  l.log("Keyboard driver initialized successfully");

  // Create server and set backend pointer
  KeyboardServerImpl server;
  server.backend = &backend;

  // Run IPC server loop
  server.run();
}
