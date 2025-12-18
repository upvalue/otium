#ifndef OT_USER_KEYBOARD_BACKEND_WASM_HPP
#define OT_USER_KEYBOARD_BACKEND_WASM_HPP

#include "ot/user/keyboard/backend.hpp"
#include "ot/user/user.hpp"

#include <emscripten.h>

// EM_JS shims for keyboard operations
// These call into JavaScript, which handles browser (keydown/keyup) or Node.js (stdin) events
// clang-format off
EM_JS(bool, js_keyboard_init, (), {
  console.log('[EM_JS] js_keyboard_init called');
  if (typeof Module.keyboardInit === 'function') {
    console.log('[EM_JS] Calling Module.keyboardInit');
    const result = Module.keyboardInit();
    console.log('[EM_JS] Module.keyboardInit returned', result);
    return result;
  }
  console.error('[EM_JS] Module.keyboardInit not defined');
  return false;
});

EM_JS(bool, js_keyboard_poll, (uint16_t *out_code, uint8_t *out_flags), {
  if (typeof Module.keyboardPoll === 'function') {
    const event = Module.keyboardPoll();
    if (event) {
      HEAPU16[out_code >> 1] = event.code;
      HEAPU8[out_flags] = event.flags;
      return true;
    }
  }
  return false;
});

EM_JS(void, js_keyboard_cleanup, (), {
  if (typeof Module.keyboardCleanup === 'function') {
    Module.keyboardCleanup();
  }
});
// clang-format on

/**
 * WASM keyboard backend using EM_JS shims.
 * Supports both browser (keydown/keyup events) and Node.js (stdin) via JavaScript callbacks.
 */
class WasmKeyboardBackend : public KeyboardBackend {
private:
  bool initialized;

public:
  WasmKeyboardBackend() : initialized(false) {}

  ~WasmKeyboardBackend() {
    if (initialized) {
      js_keyboard_cleanup();
    }
  }

  bool init() override {
    initialized = js_keyboard_init();
    return initialized;
  }

  bool poll_key(KeyEvent *out_event) override {
    if (!initialized) {
      return false;
    }

    uint16_t code = 0;
    uint8_t flags = 0;

    if (js_keyboard_poll(&code, &flags)) {
      out_event->code = code;
      out_event->flags = flags;
      out_event->reserved = 0;
      return true;
    }

    return false;
  }
};

#endif
