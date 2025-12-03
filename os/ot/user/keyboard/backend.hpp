#ifndef OT_USER_KEYBOARD_BACKEND_HPP
#define OT_USER_KEYBOARD_BACKEND_HPP

#include <stdint.h>

/**
 * Key event flags
 */
#define KEY_FLAG_PRESSED 0x01  // 1 = key down, 0 = key up
#define KEY_FLAG_SHIFT 0x02    // 1 = shift held
#define KEY_FLAG_CTRL 0x04     // 1 = ctrl held
#define KEY_FLAG_ALT 0x08      // 1 = alt held

/**
 * Key event structure
 */
struct KeyEvent {
  uint16_t code;     // Linux input scan code
  uint8_t flags;     // KEY_FLAG_* bits
  uint8_t reserved;  // Padding for alignment
};

/**
 * Modifier keys (Linux input codes)
 */
#define KEY_LEFTSHIFT 42
#define KEY_RIGHTSHIFT 54
#define KEY_LEFTCTRL 29
#define KEY_RIGHTCTRL 97
#define KEY_LEFTALT 56
#define KEY_RIGHTALT 100

/**
 * Common keys (Linux input codes)
 */
#define KEY_ESC 1
#define KEY_1 2
#define KEY_2 3
#define KEY_3 4
#define KEY_4 5
#define KEY_5 6
#define KEY_6 7
#define KEY_7 8
#define KEY_8 9
#define KEY_9 10
#define KEY_0 11
#define KEY_MINUS 12
#define KEY_EQUAL 13
#define KEY_BACKSPACE 14
#define KEY_TAB 15
#define KEY_Q 16
#define KEY_W 17
#define KEY_E 18
#define KEY_R 19
#define KEY_T 20
#define KEY_Y 21
#define KEY_U 22
#define KEY_I 23
#define KEY_O 24
#define KEY_P 25
#define KEY_LEFTBRACE 26
#define KEY_RIGHTBRACE 27
#define KEY_ENTER 28
#define KEY_A 30
#define KEY_S 31
#define KEY_D 32
#define KEY_F 33
#define KEY_G 34
#define KEY_H 35
#define KEY_J 36
#define KEY_K 37
#define KEY_L 38
#define KEY_SEMICOLON 39
#define KEY_APOSTROPHE 40
#define KEY_GRAVE 41
#define KEY_BACKSLASH 43
#define KEY_Z 44
#define KEY_X 45
#define KEY_C 46
#define KEY_V 47
#define KEY_B 48
#define KEY_N 49
#define KEY_M 50
#define KEY_COMMA 51
#define KEY_DOT 52
#define KEY_SLASH 53
#define KEY_SPACE 57

/**
 * Abstract keyboard backend interface.
 * Implementations provide platform-specific keyboard input backends for user-space.
 */
class KeyboardBackend {
public:
  virtual ~KeyboardBackend() {}

  /**
   * Initialize the keyboard backend.
   * @return true on success, false on failure
   */
  virtual bool init() = 0;

  /**
   * Poll for a keyboard event.
   * @param out_event Pointer to KeyEvent structure to fill
   * @return true if an event was available, false if no event
   */
  virtual bool poll_key(KeyEvent *out_event) = 0;
};

#endif
