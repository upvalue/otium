// edit.hpp - Text editor interface
#ifndef OT_USER_EDIT_HPP
#define OT_USER_EDIT_HPP

#include "ot/common.h"
#include "ot/lib/result.hpp"
#include "ot/lib/string-view.hpp"
#include "ot/user/string.hpp"
#include "ot/user/vector.hpp"

// Forward declarations
namespace tcl {
struct Interp;
}

namespace edit {

// Forward declarations
struct Backend;
struct Key;
enum class Action;

enum class EditorMode {
  NORMAL,
  INSERT,
  COMMND,
};

// Editor style (keybinding mode)
enum class EditorStyle {
  SIMPLE, // Non-vim mode (default) - starts in INSERT, emacs-style keybindings
  VIM,    // Traditional vim mode - starts in NORMAL, vim keybindings
};

// Operators that can be combined with motions (forward declaration for Editor)
enum class Operator {
  NONE,
  DELETE, // d
  // Future: YANK, CHANGE, etc.
};

struct Editor {
  Editor()
      : row_offset(0), col_offset(0), cx(0), cy(0), rx(0), dirty(0), last_message_time(0), mode(EditorMode::INSERT),
        pending_operator(Operator::NONE), style(EditorStyle::SIMPLE), be(nullptr), interp(nullptr), running(true) {}

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
  Operator pending_operator; // Set when waiting for motion (e.g., after 'd')
  EditorStyle style;         // Current keybinding style (SIMPLE or VIM)

  // Runtime state (set by edit_main)
  Backend *be;
  tcl::Interp *interp;
  bool running;

  // Screen management
  void screenResetLines() {
    for (size_t i = 0; i < lines.size(); i++) {
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

  // Editor operations (converted from freestanding functions)
  void execute_motion(Action action);
  void execute_action(Action action, const Key &key);
  void process_key_press();
  void scroll();
  void insert_char(char c);
  void backspace();
  void insert_newline();
  void interpret_command();
  void message_set(const ou::string &msg);
  void message_clear();
  void generate_status_line();
  void delete_line(intptr_t line);
  void apply_operator(Operator op, intptr_t start_x, intptr_t start_y, intptr_t end_x, intptr_t end_y);
  intptr_t cx_to_rx(intptr_t cx);
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
  Key() : c(0), ext(ExtendedKey::NONE), ctrl(false), alt(false) {}

  char c;
  ExtendedKey ext;

  bool ctrl;
  bool alt;
};

// Helper functions for building key sequences (useful for testing)
inline Key key_char(char c) {
  Key k;
  k.c = c;
  return k;
}
inline Key key_ctrl(char c) {
  Key k;
  k.c = c;
  k.ctrl = true;
  return k;
}
inline Key key_alt(char c) {
  Key k;
  k.c = c;
  k.alt = true;
  return k;
}
inline Key key_ext(ExtendedKey ext) {
  Key k;
  k.ext = ext;
  return k;
}

// Convenience aliases
inline Key key_esc() { return key_ext(ExtendedKey::ESC_KEY); }
inline Key key_enter() { return key_ext(ExtendedKey::ENTER_KEY); }
inline Key key_backspace() { return key_ext(ExtendedKey::BACKSPACE_KEY); }
inline Key key_up() { return key_ext(ExtendedKey::ARROW_UP); }
inline Key key_down() { return key_ext(ExtendedKey::ARROW_DOWN); }
inline Key key_left() { return key_ext(ExtendedKey::ARROW_LEFT); }
inline Key key_right() { return key_ext(ExtendedKey::ARROW_RIGHT); }

// Actions that can be triggered by keybindings
enum class Action {
  NONE,

  // Movement (also used as motions for operators)
  MOVE_LEFT,
  MOVE_RIGHT,
  MOVE_UP,
  MOVE_DOWN,
  MOVE_LINE_START, // vim: 0
  MOVE_LINE_END,   // vim: $
  PAGE_UP,
  PAGE_DOWN,

  // Operators (enter operator-pending mode)
  OPERATOR_DELETE, // vim: d

  // Mode changes
  ENTER_INSERT_MODE,
  ENTER_COMMAND_MODE,
  EXIT_TO_NORMAL,

  // Editing (INSERT mode)
  INSERT_NEWLINE,
  DELETE_CHAR_BACK,

  // Command mode
  COMMAND_EXECUTE,
  COMMAND_BACKSPACE,

  // Global
  FORCE_QUIT,
};

// Special mode value for bindings that work in any mode
constexpr EditorMode ANY_MODE = static_cast<EditorMode>(-1);

// Keybinding entry mapping a key to an action
struct Keybinding {
  Key key;
  EditorMode mode;
  Action action;
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
  virtual void render(const Editor &ed) = 0;

  /** Debug output to platform-specific location */
  virtual void debug_print(const ou::string &msg) = 0;

  /** Called before processing a frame; return false to skip (e.g., app not active) */
  virtual bool begin_frame() { return true; }

  /** Called after rendering */
  virtual void end_frame() {}

  /** Called at end of each loop iteration; for cooperative scheduling */
  virtual void yield() {}
};

void edit_run(Backend *backend, Editor *editor, tcl::Interp *interp, ou::string *file_path);

// Test helper - runs editor with scripted keys, returns final file contents
ou::vector<ou::string> edit_test_run(const Key *keys, size_t count,
                                     const ou::vector<ou::string> *initial_lines = nullptr,
                                     EditorStyle style = EditorStyle::VIM);

} // namespace edit

#endif