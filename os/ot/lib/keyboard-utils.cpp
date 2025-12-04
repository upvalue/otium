// keyboard-utils.cpp - Keyboard utility functions
#include "ot/lib/keyboard-utils.hpp"

namespace keyboard {

// Map key scan code to ASCII character (US-QWERTY layout with shift support)
char scancode_to_ascii(uint16_t code, bool shift) {
  // Numbers and their shift symbols
  if (code >= KEY_1 && code <= KEY_9) {
    if (shift) {
      const char shift_nums[] = "!@#$%^&*(";
      return shift_nums[code - KEY_1];
    }
    return '1' + (code - KEY_1);
  }
  if (code == KEY_0) {
    return shift ? ')' : '0';
  }

  // Top row letters (Q-P)
  if (code == KEY_Q)
    return shift ? 'Q' : 'q';
  if (code == KEY_W)
    return shift ? 'W' : 'w';
  if (code == KEY_E)
    return shift ? 'E' : 'e';
  if (code == KEY_R)
    return shift ? 'R' : 'r';
  if (code == KEY_T)
    return shift ? 'T' : 't';
  if (code == KEY_Y)
    return shift ? 'Y' : 'y';
  if (code == KEY_U)
    return shift ? 'U' : 'u';
  if (code == KEY_I)
    return shift ? 'I' : 'i';
  if (code == KEY_O)
    return shift ? 'O' : 'o';
  if (code == KEY_P)
    return shift ? 'P' : 'p';

  // Home row letters (A-L)
  if (code == KEY_A)
    return shift ? 'A' : 'a';
  if (code == KEY_S)
    return shift ? 'S' : 's';
  if (code == KEY_D)
    return shift ? 'D' : 'd';
  if (code == KEY_F)
    return shift ? 'F' : 'f';
  if (code == KEY_G)
    return shift ? 'G' : 'g';
  if (code == KEY_H)
    return shift ? 'H' : 'h';
  if (code == KEY_J)
    return shift ? 'J' : 'j';
  if (code == KEY_K)
    return shift ? 'K' : 'k';
  if (code == KEY_L)
    return shift ? 'L' : 'l';

  // Bottom row letters (Z-M)
  if (code == KEY_Z)
    return shift ? 'Z' : 'z';
  if (code == KEY_X)
    return shift ? 'X' : 'x';
  if (code == KEY_C)
    return shift ? 'C' : 'c';
  if (code == KEY_V)
    return shift ? 'V' : 'v';
  if (code == KEY_B)
    return shift ? 'B' : 'b';
  if (code == KEY_N)
    return shift ? 'N' : 'n';
  if (code == KEY_M)
    return shift ? 'M' : 'm';

  // Punctuation keys
  if (code == KEY_COMMA)
    return shift ? '<' : ',';
  if (code == KEY_DOT)
    return shift ? '>' : '.';
  if (code == KEY_SLASH)
    return shift ? '?' : '/';
  if (code == KEY_SEMICOLON)
    return shift ? ':' : ';';
  if (code == KEY_APOSTROPHE)
    return shift ? '"' : '\'';
  if (code == KEY_LEFTBRACE)
    return shift ? '{' : '[';
  if (code == KEY_RIGHTBRACE)
    return shift ? '}' : ']';
  if (code == KEY_BACKSLASH)
    return shift ? '|' : '\\';
  if (code == KEY_MINUS)
    return shift ? '_' : '-';
  if (code == KEY_EQUAL)
    return shift ? '+' : '=';
  if (code == KEY_GRAVE)
    return shift ? '~' : '`';

  // Special keys
  if (code == KEY_SPACE)
    return ' ';
  if (code == KEY_ENTER)
    return '\n';
  if (code == KEY_TAB)
    return '\t';

  return 0; // Unmapped key
}

} // namespace keyboard
