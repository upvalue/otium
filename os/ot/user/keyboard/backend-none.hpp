#ifndef OT_USER_KEYBOARD_BACKEND_NONE_HPP
#define OT_USER_KEYBOARD_BACKEND_NONE_HPP

#include "ot/user/keyboard/backend.hpp"

/**
 * No-op keyboard backend.
 * Always returns false on poll_key (no keys available).
 */
class NoneKeyboardBackend : public KeyboardBackend {
public:
  bool init() override { return true; }

  bool poll_key(KeyEvent *out_event) override {
    (void)out_event;
    return false;  // No keys ever available
  }
};

#endif
