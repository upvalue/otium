// tevl.hpp - TEVL text editor interface
#ifndef OT_USER_TEVL_HPP
#define OT_USER_TEVL_HPP

#include "ot/common.h"
#include "ot/shared/result.hpp"
#include "ot/shared/string-view.hpp"
#include "ot/user/string.hpp"
#include "ot/user/vector.hpp"

namespace tevl {

enum class EditorMode {
  NORMAL,
  INSERT,
};

struct Editor {
  Editor() : cx(0), cy(0), mode(EditorMode::NORMAL) {}

  int cx, cy;
  /** Lines to render; note that this is only roughly the height of the screen */
  ou::vector<ou::string> lines;

  ou::vector<ou::string> file_lines;

  EditorMode mode;

  void resetLines() {
    for (int i = 0; i < lines.size(); i++) {
      lines[i].clear();
    }
  }

  /** Overwrite a given row; grows if needed */
  void putLine(int y, const ou::string &line) {
    if (y >= lines.size()) {
      while (lines.size() <= y) {
        lines.push_back(ou::string());
      }
    }
    lines[y] = line;
  }

  /** Append to a given row; grows if needed */
  void appendLine(int y, const ou::string &line) {
    if (y >= lines.size()) {
      while (lines.size() <= y) {
        lines.push_back(ou::string());
      }
    }
    lines[y] += line;
  }
};

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
  virtual void render(int cx, int cy, const ou::vector<ou::string> &lines) = 0;
  virtual Result<Coord, EditorErr> getCursorPosition() = 0;
};

void tevl_main(Backend *backend);

#endif
}