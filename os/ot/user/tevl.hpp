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
  COMMND,
};

struct Editor {
  Editor() : row_offset(0), col_offset(0), cx(0), cy(0), mode(EditorMode::NORMAL), dirty(0) {}

  intptr_t row_offset, col_offset;

  /** Cursor position on the screen */
  intptr_t cx, cy;
  intptr_t rx;
  /** how many times file has been modified */
  intptr_t dirty;

  /** Lines to render; note that this is only roughly the height of the screen */
  ou::vector<ou::string> lines;
  ou::vector<ou::string> file_lines;
  ou::vector<ou::string> render_lines;

  ou::string file_name;
  /** status line -- shows info like current col, active file */
  ou::string status_line;
  /** message line -- shows either a text notif */
  ou::string message_line;
  uint64_t last_message_time;

  ou::string command_line;

  EditorMode mode;

  void screenResetLines() {
    for (int i = 0; i < lines.size(); i++) {
      lines[i].clear();
    }
  }

  /** Overwrite a given row; grows if needed.  */
  void screenPutLine(int y, const ou::string &line, size_t cutoff = 0);

  /** Append to a given row; grows if needed */
  void screenAppendLine(int y, const ou::string &line) {
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
  ENTER_KEY,
  BACKSPACE_KEY,
  ARROW_LEFT,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN,
  DEL_KEY,
  ESC_KEY,
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
  // virtual void render(intptr_t col_offset, int cx, int cy, const ou::vector<ou::string> &lines) = 0;
  virtual void render(const Editor &ed) = 0;
  virtual Result<Coord, EditorErr> getCursorPosition() = 0;

  /** Debug output to platform-specific location */
  virtual void debug_print(const ou::string &msg) = 0;
};

void tevl_main(Backend *backend, ou::string *file_path);

#endif
}