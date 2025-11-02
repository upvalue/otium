// tevl.h - TEVL text editor interface
#ifndef OT_USER_TEVL_H
#define OT_USER_TEVL_H

#include "ot/common.h"
#include "ot/shared/result.hpp"

namespace tevl {

enum class EditorErr {
  NONE,

  FATAL_TERM_READ_KEY_FAILED,
  FATAL_TERM_TCSETATTR_FAILED,
};

struct Backend {
  const char *error_msg;

  /** Checks for keyboard input; does not block */
  virtual Result<char, EditorErr> readKey() = 0;

  virtual EditorErr setup() = 0;
  virtual void teardown() = 0;
};

void tevl_main(Backend *backend);

#endif
}