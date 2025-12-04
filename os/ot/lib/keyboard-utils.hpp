// keyboard-utils.hpp - Keyboard utility functions
#pragma once

#include "ot/user/keyboard/backend.hpp"

namespace keyboard {

// Map key scan code to ASCII character (US-QWERTY layout with shift support)
char scancode_to_ascii(uint16_t code, bool shift);

} // namespace keyboard
