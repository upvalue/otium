// tevl.cpp - TEVL text editor core implementation

#include <stdio.h>

#include "ot/common.h"
#include "ot/lib/file.hpp"
#include "ot/lib/result.hpp"
#include "ot/user/string.hpp"
#include "ot/user/tcl.hpp"
#include "ot/user/tevl.hpp"
#include "ot/user/user.hpp"

bool running = true;
static const char *default_error_msg = "no error message set";
tevl::Backend *be = nullptr;
static int TAB_SIZE = 4;
static int MESSAGE_TIMEOUT_MS = 3000;

ou::string buffer;

using namespace tevl;
using namespace ou;

// Default keybinding table
static Keybinding default_bindings[] = {
    // Global (any mode)
    {key_ctrl('d'), ANY_MODE, Action::PAGE_DOWN},
    {key_ctrl('u'), ANY_MODE, Action::PAGE_UP},

    // NORMAL + INSERT mode movement (arrow keys)
    {key_left(), EditorMode::NORMAL, Action::MOVE_LEFT},
    {key_right(), EditorMode::NORMAL, Action::MOVE_RIGHT},
    {key_up(), EditorMode::NORMAL, Action::MOVE_UP},
    {key_down(), EditorMode::NORMAL, Action::MOVE_DOWN},
    {key_left(), EditorMode::INSERT, Action::MOVE_LEFT},
    {key_right(), EditorMode::INSERT, Action::MOVE_RIGHT},
    {key_up(), EditorMode::INSERT, Action::MOVE_UP},
    {key_down(), EditorMode::INSERT, Action::MOVE_DOWN},

    // NORMAL mode - motions
    {key_char('h'), EditorMode::NORMAL, Action::MOVE_LEFT},
    {key_char('j'), EditorMode::NORMAL, Action::MOVE_DOWN},
    {key_char('k'), EditorMode::NORMAL, Action::MOVE_UP},
    {key_char('l'), EditorMode::NORMAL, Action::MOVE_RIGHT},
    {key_char('0'), EditorMode::NORMAL, Action::MOVE_LINE_START},
    {key_char('$'), EditorMode::NORMAL, Action::MOVE_LINE_END},

    // NORMAL mode - operators
    {key_char('d'), EditorMode::NORMAL, Action::OPERATOR_DELETE},

    // NORMAL mode - other
    {key_char('i'), EditorMode::NORMAL, Action::ENTER_INSERT_MODE},
    {key_char(';'), EditorMode::NORMAL, Action::ENTER_COMMAND_MODE},

    // INSERT mode specific
    {key_esc(), EditorMode::INSERT, Action::EXIT_TO_NORMAL},
    {key_enter(), EditorMode::INSERT, Action::INSERT_NEWLINE},
    {key_backspace(), EditorMode::INSERT, Action::DELETE_CHAR_BACK},

    // COMMAND mode specific
    {key_enter(), EditorMode::COMMND, Action::COMMAND_EXECUTE},
    {key_backspace(), EditorMode::COMMND, Action::COMMAND_BACKSPACE},
};
static const size_t num_bindings = sizeof(default_bindings) / sizeof(default_bindings[0]);

// Compare two keys for equality
bool keys_match(const Key &a, const Key &b) {
  if (a.ext != ExtendedKey::NONE || b.ext != ExtendedKey::NONE) {
    return a.ext == b.ext && a.ctrl == b.ctrl;
  }
  return a.c == b.c && a.ctrl == b.ctrl;
}

// Look up action for a key in the given mode
Action lookup_action(EditorMode mode, const Key &key) {
  for (size_t i = 0; i < num_bindings; i++) {
    const Keybinding &binding = default_bindings[i];
    if (binding.mode != ANY_MODE && binding.mode != mode) {
      continue;
    }
    if (keys_match(binding.key, key)) {
      return binding.action;
    }
  }
  return Action::NONE;
}

// Check if an action is a motion (can be used with operators)
bool is_motion(Action action) {
  switch (action) {
  case Action::MOVE_LEFT:
  case Action::MOVE_RIGHT:
  case Action::MOVE_UP:
  case Action::MOVE_DOWN:
  case Action::MOVE_LINE_START:
  case Action::MOVE_LINE_END:
    return true;
  default:
    return false;
  }
}

// Forward declarations
extern Editor e;
extern tevl::Backend *be;

// Execute a motion action (moves the cursor)
void execute_motion(Action action) {
  switch (action) {
  case Action::MOVE_LEFT:
    if (e.cx != 0) {
      e.cx--;
    } else if (e.cy > 0) {
      e.cy--;
      e.cx = e.file_lines[e.cy].length();
    }
    break;
  case Action::MOVE_RIGHT:
    if (e.cy < e.file_lines.size() && e.cx < e.file_lines[e.cy].length()) {
      e.cx++;
    } else if (e.cy < e.file_lines.size() - 1) {
      e.cy++;
      e.cx = 0;
    }
    break;
  case Action::MOVE_UP:
    if (e.cy > 0) {
      e.cy--;
    }
    break;
  case Action::MOVE_DOWN:
    if (e.cy < e.file_lines.size() - 1) {
      e.cy++;
    }
    break;
  case Action::MOVE_LINE_START:
    e.cx = 0;
    break;
  case Action::MOVE_LINE_END:
    if (e.cy < e.file_lines.size()) {
      e.cx = e.file_lines[e.cy].length();
    }
    break;
  case Action::PAGE_UP: {
    auto ws = be->getWindowSize();
    int page_size = ws.y / 2;
    e.cy -= page_size;
    if (e.cy < 0) {
      e.cy = 0;
    }
    break;
  }
  case Action::PAGE_DOWN: {
    auto ws = be->getWindowSize();
    int page_size = ws.y / 2;
    e.cy += page_size;
    if (e.cy >= e.file_lines.size()) {
      e.cy = e.file_lines.size() - 1;
    }
    break;
  }
  default:
    break;
  }
}

// Delete an entire line
void delete_line(intptr_t line) {
  if (line < e.file_lines.size()) {
    e.file_lines.erase(line);
    if (e.file_lines.empty()) {
      e.file_lines.push_back(ou::string());
    }
    if (e.cy >= e.file_lines.size()) {
      e.cy = e.file_lines.size() - 1;
    }
    e.cx = 0;
    e.dirty++;
  }
}

// Apply operator to a range (same line only for now)
void apply_operator(Operator op, intptr_t start_x, intptr_t start_y, intptr_t end_x, intptr_t end_y) {
  if (op == Operator::DELETE) {
    if (start_y == end_y && start_y < e.file_lines.size()) {
      // Same line: delete characters between start_x and end_x
      if (start_x > end_x) {
        intptr_t tmp = start_x;
        start_x = end_x;
        end_x = tmp;
      }
      e.file_lines[start_y].erase(start_x, end_x - start_x);
      e.cx = start_x;
      e.dirty++;
    }
    // Multi-line delete can be added later
  }
}

// Forward declarations for functions used by execute_action
void editor_insert_newline();
void editor_backspace();
void editor_interpret_command();

// Execute a non-motion action
void execute_action(Action action, const Key &key) {
  switch (action) {
  case Action::OPERATOR_DELETE:
    e.pending_operator = Operator::DELETE;
    break;
  case Action::ENTER_INSERT_MODE:
    e.mode = EditorMode::INSERT;
    break;
  case Action::ENTER_COMMAND_MODE:
    e.mode = EditorMode::COMMND;
    e.command_line.clear();
    break;
  case Action::EXIT_TO_NORMAL:
    e.mode = EditorMode::NORMAL;
    break;
  case Action::INSERT_NEWLINE:
    editor_insert_newline();
    break;
  case Action::DELETE_CHAR_BACK:
    editor_backspace();
    break;
  case Action::COMMAND_EXECUTE:
    editor_interpret_command();
    e.command_line.clear();
    e.mode = EditorMode::NORMAL;
    break;
  case Action::COMMAND_BACKSPACE:
    if (!e.command_line.empty()) {
      e.command_line.erase(e.command_line.length() - 1, 1);
    }
    break;
  case Action::FORCE_QUIT:
    running = false;
    break;
  default:
    break;
  }
}

void Editor::screenPutLine(int y, const ou::string &line, size_t cutoff) {
  while (lines.size() <= y) {
    lines.push_back(ou::string());
  }
  while (render_lines.size() <= y) {
    render_lines.push_back(ou::string());
  }
  if (cutoff != 0) {
    lines[y] = line.substr(0, cutoff);
  } else {
    lines[y] = line;
  }

  // Handle rendering lines
  render_lines[y].ensure_capacity(line.length());
  render_lines[y].clear();
  for (size_t i = 0; i < line.length(); i++) {
    if (line[i] == '\t') {
      for (size_t j = 0; j < TAB_SIZE; j++) {
        render_lines[y].push_back(' ');
      }
    } else {
      render_lines[y].push_back(line[i]);
    }
  }
}

Editor e;
tcl::Interp interp;

void editor_message_set(const ou::string &message) {
  e.message_line = message;
  e.last_message_time = o_time_get();
}

void editor_interpret_command() {
  if (e.command_line.empty()) {
    return;
  }

  be->debug_print("evaluating command");
  be->debug_print(e.command_line);

  tcl::Status status = interp.eval(e.command_line);
  if (status != tcl::S_OK) {
    editor_message_set(interp.result);
  }
}

void editor_insert_char(char c) {
  if (e.cy == e.file_lines.size()) {
    e.file_lines.push_back(ou::string());
  }
  e.file_lines[e.cy].insert(e.cx, 1, c);
  e.cx++;
  e.dirty++;
}

void editor_backspace() {
  if (e.cx > 0 && e.cy < e.file_lines.size()) {
    e.file_lines[e.cy].erase(e.cx - 1, 1);
    e.cx--;
  } else if (e.cy > 0) {
    // handle user going back onto previous line
    e.cy--;
    e.cx = e.file_lines[e.cy].length();
    e.file_lines[e.cy].append(e.file_lines[e.cy + 1]);
    e.file_lines.erase(e.cy + 1);
  }
  e.dirty++;
}

void editor_insert_newline() {
  // Handle case whre there is text after
  if (e.cx < e.file_lines[e.cy].length()) {

    ou::string text = e.file_lines[e.cy].substr(e.cx);
    e.file_lines[e.cy].erase(e.cx);
    e.file_lines.insert(e.cy + 1, text);
  } else {
    e.file_lines.insert(e.cy + 1, ou::string());
  }
  e.cy++;
  e.cx = 0;
}

// Check if a key is empty (no input)
bool is_empty_key(const Key &k) {
  return k.c == 0 && k.ext == ExtendedKey::NONE && !k.ctrl;
}

void process_key_press() {
  auto res = be->readKey();

  if (res.is_err()) {
    oprintf("failed to read key errcode=%d\n", res.error());
    return;
  }

  Key k = res.value();

  // Ignore empty keys (timeout with no input)
  if (is_empty_key(k)) {
    return;
  }

  Action action = lookup_action(e.mode, k);

  // Handle operator-pending state
  if (e.pending_operator != Operator::NONE) {
    if (is_motion(action)) {
      // Calculate motion endpoint
      intptr_t start_x = e.cx, start_y = e.cy;
      execute_motion(action);
      apply_operator(e.pending_operator, start_x, start_y, e.cx, e.cy);
      e.pending_operator = Operator::NONE;
    } else if (action == Action::OPERATOR_DELETE && e.pending_operator == Operator::DELETE) {
      // dd - delete entire line
      delete_line(e.cy);
      e.pending_operator = Operator::NONE;
    } else {
      // Cancel on Esc or any other key
      e.pending_operator = Operator::NONE;
    }
  } else {
    // Normal execution
    if (action != Action::NONE) {
      if (is_motion(action)) {
        execute_motion(action);
      } else {
        execute_action(action, k);
      }
    } else {
      // Fallback for unbound printable chars
      if (e.mode == EditorMode::INSERT && k.c >= 32 && k.c <= 126) {
        editor_insert_char(k.c);
      } else if (e.mode == EditorMode::COMMND && k.c >= 32 && k.c <= 126) {
        e.command_line.push_back(k.c);
      }
    }
  }

  // Correct cx if it's beyond the end of the current line
  if (e.cy < e.file_lines.size()) {
    intptr_t current_line_len = e.file_lines[e.cy].length();
    if (e.cx > current_line_len) {
      e.cx = current_line_len;
    }
  }
}

intptr_t editor_cx_to_rx(intptr_t cx) {
  intptr_t rx = 0;
  intptr_t j;
  for (j = 0; j < cx; j++) {
    if (e.file_lines[e.cy][j] == '\t') {
      rx += (TAB_SIZE - 1) - (rx % TAB_SIZE);
    }
    rx++;
  }

  return rx;
}

void editor_scroll() {
  auto ws = be->getWindowSize();
  e.rx = editor_cx_to_rx(e.cx);

  // Handle scroll
  if (e.cy < e.row_offset) {
    e.row_offset = e.cy;
  }

  if (e.cy >= e.row_offset + ws.y) {
    e.row_offset = e.cy - ws.y + 1;
  }

  if (e.rx < e.col_offset) {
    e.col_offset = e.rx;
  }

  if (e.rx >= e.col_offset + ws.x) {
    e.col_offset = e.rx - ws.x + 1;
  }
}

void editor_message_clear() {
  uint64_t now = o_time_get();
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "now: %d, last_message_time: %d", now, e.last_message_time);
  be->debug_print(buffer);
  if (o_time_get() - e.last_message_time > MESSAGE_TIMEOUT_MS) {
    e.message_line.clear();
  }
}

void generate_status_line() {
  e.status_line.clear();
  if (e.mode == EditorMode::INSERT) {
    e.status_line.append("[insert] ");
  } else if (e.mode == EditorMode::COMMND) {
    e.status_line.append("[commnd] ");
  } else if (e.mode == EditorMode::NORMAL) {
    if (e.pending_operator == Operator::DELETE) {
      e.status_line.append("[normal d] ");
    } else {
      e.status_line.append("[normal] ");
    }
  } else {
    e.status_line.append("[normal] ");
  }
  e.status_line.append(e.file_name);
  if (e.dirty > 0) {
    e.status_line.append("*");
  } else {
    e.status_line.append(" ");
  }
  e.status_line.append(" ");

  // cx/cy
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%d/%d", e.cy + 1, e.cx + 1);
  e.status_line.append(buffer);
  e.status_line.append(" ");
}

// tcl commands
tcl::Status tcl_command_hard_quit(tcl::Interp &interp, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  running = false;
  return tcl::S_OK;
}

tcl::Status tcl_command_quit(tcl::Interp &interp, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (e.dirty > 0) {
    interp.result = "file has changes, use q! to quit";
    return tcl::S_ERR;
  }
  return tcl_command_hard_quit(interp, argv, privdata);
}

tcl::Status tcl_command_write(tcl::Interp &interp, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (e.file_name.empty()) {
    interp.result = "no filename";
    return tcl::S_ERR;
  }

  ou::File file(e.file_name.c_str(), ou::FileMode::WRITE);
  ErrorCode err = file.open();
  if (err != ErrorCode::NONE) {
    interp.result = "failed to open file for writing";
    return tcl::S_ERR;
  }

  for (size_t i = 0; i < e.file_lines.size(); i++) {
    err = file.write(e.file_lines[i]);
    if (err != ErrorCode::NONE) {
      interp.result = "failed to write line";
      return tcl::S_ERR;
    }
    if (i < e.file_lines.size() - 1) {
      err = file.write("\n");
      if (err != ErrorCode::NONE) {
        interp.result = "failed to write newline";
        return tcl::S_ERR;
      }
    }
  }

  e.dirty = 0;
  editor_message_set("file written");
  return tcl::S_OK;
}

namespace tevl {

void tevl_main(Backend *be_, ou::string *file_path) {
  tcl::register_core_commands(interp);

  interp.register_command("q", tcl_command_quit);
  interp.register_command("q!", tcl_command_hard_quit);
  interp.register_command("quit", tcl_command_quit);
  interp.register_command("quit!", tcl_command_hard_quit);

  interp.register_command("write", tcl_command_write);
  interp.register_command("w", tcl_command_write);

  be = be_;
  be->error_msg = default_error_msg;
  if (be->setup() != EditorErr::NONE) {
    oprintf("failed to setup be: %s\n", be->error_msg);
    return;
  }

  ou::string tilde("~");

  if (file_path) {
    ou::File file(file_path->c_str());
    e.file_name = file_path->c_str();
    ErrorCode err = file.open();
    if (err != ErrorCode::NONE) {
      oprintf("failed to open file %s: %d\n", file_path->c_str(), err);
      return;
    }

    file.forEachLine([](const ou::string &line) {
      // be->debug_print("adding line to file: ");
      e.file_lines.push_back(line);
    });
  }

  while (running) {
    // Handle scroll
    auto ws = be->getWindowSize();
    editor_scroll();

    // Handle message clear
    editor_message_clear();

    // Render individual rows
    e.screenResetLines();

    generate_status_line();

    for (int y = 0; y < ws.y; y++) {
      int file_row = y + e.row_offset;
      if (file_row < e.file_lines.size()) {
        intptr_t len = e.file_lines[file_row].length() - e.col_offset;
        if (len < 0) {
          len = 0;
        }
        if (len > ws.x) {
          len = ws.x;
        }
        e.screenPutLine(y, e.file_lines[file_row].substr(e.col_offset, len), len);
      } else {
        e.screenPutLine(y, tilde);
      }
    }

    be->render(e);
    // Handle user input
    process_key_press();
  }

  be->teardown();

  if (be->error_msg != default_error_msg) {
    oprintf("error: %s\n", be->error_msg);
  }

  be->clear();
  return;
}

} // namespace tevl