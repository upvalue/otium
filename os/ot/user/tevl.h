// tevl.h - TEVL text editor interface
#ifndef OT_USER_TEVL_H
#define OT_USER_TEVL_H

#include "ot/common.h"
#include "ot/shared/result.hpp"
#include "ot/shared/string-view.hpp"
#include "ot/user/string.hpp"
#include "ot/user/vector.hpp"

namespace tevl {

enum class EditorErr {
  NONE,

  FATAL_TERM_READ_KEY_FAILED,
  FATAL_TERM_TCSETATTR_FAILED,
  FATAL_TERM_GET_CURSOR_POSITION_FAILED,
};

struct Coord {
  int x, y;
};

enum ExtendedKey {
  NONE,
  ARROW_LEFT,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  DEL_KEY,
};

struct Key {
  Key() : c(0), ext(ExtendedKey::NONE), ctrl(false) {}

  char c;
  ExtendedKey ext;

  bool ctrl;
};

struct Backend {
  const char *error_msg;

  /** Checks for keyboard input; does not block */
  virtual Result<Key, EditorErr> readKey() = 0;

  virtual EditorErr setup() = 0;
  virtual void teardown() = 0;
  virtual void refresh() = 0;
  virtual void clear() = 0;
  virtual Coord getWindowSize() = 0;
  virtual void render(const ou::vector<ou::string> &lines) = 0;
  virtual Result<Coord, EditorErr> getCursorPosition() = 0;
};

void tevl_main(Backend *backend);

#endif
}