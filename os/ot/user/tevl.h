// tevl.h - TEVL text editor interface
#ifndef OT_USER_TEVL_H
#define OT_USER_TEVL_H

#include "ot/common.h"
#include "ot/shared/result.hpp"
#include "ot/shared/string-view.hpp"
#include "ot/user/string.hpp"

namespace tevl {

enum class EditorErr {
  NONE,

  FATAL_TERM_READ_KEY_FAILED,
  FATAL_TERM_TCSETATTR_FAILED,
};

struct Coord {
  int x, y;
};

struct Key {
  Key() : c(0), ctrl(false) {}

  char c;

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
  virtual void render(const ou::string &s) = 0;
};

void tevl_main(Backend *backend);

#endif
}