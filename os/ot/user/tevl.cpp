// tevl.cpp - TEVL text editor core implementation

#include <stdio.h>

#include "ot/common.h"
#include "ot/lib/file.hpp"
#include "ot/lib/result.hpp"
#include "ot/user/string.hpp"
#include "ot/user/tcl.hpp"
#include "ot/user/tevl.hpp"
#include "ot/user/user.hpp"

static const char *default_error_msg = "no error message set";
static int TAB_SIZE = 4;
static int MESSAGE_TIMEOUT_MS = 3000;

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
static bool keys_match(const Key &a, const Key &b) {
  if (a.ext != ExtendedKey::NONE || b.ext != ExtendedKey::NONE) {
    return a.ext == b.ext && a.ctrl == b.ctrl;
  }
  return a.c == b.c && a.ctrl == b.ctrl;
}

// Look up action for a key in the given mode
static Action lookup_action(EditorMode mode, const Key &key) {
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
static bool is_motion(Action action) {
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

// Check if a key is empty (no input)
static bool is_empty_key(const Key &k) {
  return k.c == 0 && k.ext == ExtendedKey::NONE && !k.ctrl;
}

//
// Editor method implementations
//

void Editor::execute_motion(Action action) {
  switch (action) {
  case Action::MOVE_LEFT:
    if (cx != 0) {
      cx--;
    } else if (cy > 0) {
      cy--;
      cx = file_lines[cy].length();
    }
    break;
  case Action::MOVE_RIGHT:
    if (cy < file_lines.size() && cx < file_lines[cy].length()) {
      cx++;
    } else if (cy < file_lines.size() - 1) {
      cy++;
      cx = 0;
    }
    break;
  case Action::MOVE_UP:
    if (cy > 0) {
      cy--;
    }
    break;
  case Action::MOVE_DOWN:
    if (cy < file_lines.size() - 1) {
      cy++;
    }
    break;
  case Action::MOVE_LINE_START:
    cx = 0;
    break;
  case Action::MOVE_LINE_END:
    if (cy < file_lines.size()) {
      cx = file_lines[cy].length();
    }
    break;
  case Action::PAGE_UP: {
    auto ws = be->getWindowSize();
    int page_size = ws.y / 2;
    cy -= page_size;
    if (cy < 0) {
      cy = 0;
    }
    break;
  }
  case Action::PAGE_DOWN: {
    auto ws = be->getWindowSize();
    int page_size = ws.y / 2;
    cy += page_size;
    if (cy >= file_lines.size()) {
      cy = file_lines.size() - 1;
    }
    break;
  }
  default:
    break;
  }
}

void Editor::delete_line(intptr_t line) {
  if (line < file_lines.size()) {
    file_lines.erase(line);
    if (file_lines.empty()) {
      file_lines.push_back(ou::string());
    }
    if (cy >= file_lines.size()) {
      cy = file_lines.size() - 1;
    }
    cx = 0;
    dirty++;
  }
}

void Editor::apply_operator(Operator op, intptr_t start_x, intptr_t start_y, intptr_t end_x,
                            intptr_t end_y) {
  if (op == Operator::DELETE) {
    if (start_y == end_y && start_y < file_lines.size()) {
      // Same line: delete characters between start_x and end_x
      if (start_x > end_x) {
        intptr_t tmp = start_x;
        start_x = end_x;
        end_x = tmp;
      }
      file_lines[start_y].erase(start_x, end_x - start_x);
      cx = start_x;
      dirty++;
    }
    // Multi-line delete can be added later
  }
}

void Editor::execute_action(Action action, const Key &key) {
  switch (action) {
  case Action::OPERATOR_DELETE:
    pending_operator = Operator::DELETE;
    break;
  case Action::ENTER_INSERT_MODE:
    mode = EditorMode::INSERT;
    break;
  case Action::ENTER_COMMAND_MODE:
    mode = EditorMode::COMMND;
    command_line.clear();
    break;
  case Action::EXIT_TO_NORMAL:
    mode = EditorMode::NORMAL;
    break;
  case Action::INSERT_NEWLINE:
    insert_newline();
    break;
  case Action::DELETE_CHAR_BACK:
    backspace();
    break;
  case Action::COMMAND_EXECUTE:
    interpret_command();
    command_line.clear();
    mode = EditorMode::NORMAL;
    break;
  case Action::COMMAND_BACKSPACE:
    if (!command_line.empty()) {
      command_line.erase(command_line.length() - 1, 1);
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

void Editor::message_set(const ou::string &message) {
  message_line = message;
  last_message_time = o_time_get();
}

void Editor::interpret_command() {
  if (command_line.empty()) {
    return;
  }

  be->debug_print("evaluating command");
  be->debug_print(command_line);

  tcl::Status status = interp->eval(command_line);
  if (status != tcl::S_OK) {
    message_set(interp->result);
  }
}

void Editor::insert_char(char c) {
  if (cy == file_lines.size()) {
    file_lines.push_back(ou::string());
  }
  file_lines[cy].insert(cx, 1, c);
  cx++;
  dirty++;
}

void Editor::backspace() {
  if (cx > 0 && cy < file_lines.size()) {
    file_lines[cy].erase(cx - 1, 1);
    cx--;
  } else if (cy > 0) {
    // handle user going back onto previous line
    cy--;
    cx = file_lines[cy].length();
    file_lines[cy].append(file_lines[cy + 1]);
    file_lines.erase(cy + 1);
  }
  dirty++;
}

void Editor::insert_newline() {
  // Handle case where there is text after
  if (cx < file_lines[cy].length()) {
    ou::string text = file_lines[cy].substr(cx);
    file_lines[cy].erase(cx);
    file_lines.insert(cy + 1, text);
  } else {
    file_lines.insert(cy + 1, ou::string());
  }
  cy++;
  cx = 0;
}

void Editor::process_key_press() {
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

  Action action = lookup_action(mode, k);

  // Handle operator-pending state
  if (pending_operator != Operator::NONE) {
    if (is_motion(action)) {
      // Calculate motion endpoint
      intptr_t start_x = cx, start_y = cy;
      execute_motion(action);
      apply_operator(pending_operator, start_x, start_y, cx, cy);
      pending_operator = Operator::NONE;
    } else if (action == Action::OPERATOR_DELETE && pending_operator == Operator::DELETE) {
      // dd - delete entire line
      delete_line(cy);
      pending_operator = Operator::NONE;
    } else {
      // Cancel on Esc or any other key
      pending_operator = Operator::NONE;
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
      if (mode == EditorMode::INSERT && k.c >= 32 && k.c <= 126) {
        insert_char(k.c);
      } else if (mode == EditorMode::COMMND && k.c >= 32 && k.c <= 126) {
        command_line.push_back(k.c);
      }
    }
  }

  // Correct cx if it's beyond the end of the current line
  if (cy < file_lines.size()) {
    intptr_t current_line_len = file_lines[cy].length();
    if (cx > current_line_len) {
      cx = current_line_len;
    }
  }
}

intptr_t Editor::cx_to_rx(intptr_t cx_arg) {
  intptr_t result = 0;
  for (intptr_t j = 0; j < cx_arg; j++) {
    if (file_lines[cy][j] == '\t') {
      result += (TAB_SIZE - 1) - (result % TAB_SIZE);
    }
    result++;
  }
  return result;
}

void Editor::scroll() {
  auto ws = be->getWindowSize();
  rx = cx_to_rx(cx);

  // Handle scroll
  if (cy < row_offset) {
    row_offset = cy;
  }

  if (cy >= row_offset + ws.y) {
    row_offset = cy - ws.y + 1;
  }

  if (rx < col_offset) {
    col_offset = rx;
  }

  if (rx >= col_offset + ws.x) {
    col_offset = rx - ws.x + 1;
  }
}

void Editor::message_clear() {
  uint64_t now = o_time_get();
  char buffer[1024];
  snprintf(buffer, sizeof(buffer), "now: %llu, last_message_time: %llu",
           (unsigned long long)now, (unsigned long long)last_message_time);
  be->debug_print(buffer);
  if (o_time_get() - last_message_time > MESSAGE_TIMEOUT_MS) {
    message_line.clear();
  }
}

void Editor::generate_status_line() {
  status_line.clear();
  if (mode == EditorMode::INSERT) {
    status_line.append("[insert] ");
  } else if (mode == EditorMode::COMMND) {
    status_line.append("[commnd] ");
  } else if (mode == EditorMode::NORMAL) {
    if (pending_operator == Operator::DELETE) {
      status_line.append("[normal d] ");
    } else {
      status_line.append("[normal] ");
    }
  } else {
    status_line.append("[normal] ");
  }
  status_line.append(file_name);
  if (dirty > 0) {
    status_line.append("*");
  } else {
    status_line.append(" ");
  }
  status_line.append(" ");

  // cx/cy
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%ld/%ld", (long)(cy + 1), (long)(cx + 1));
  status_line.append(buffer);
  status_line.append(" ");
}

//
// TCL command privdata to access Editor
//
struct TclEditorPrivdata : public tcl::ProcPrivdata {
  Editor *editor;
  TclEditorPrivdata(Editor *e) : tcl::ProcPrivdata(), editor(e) {}
};

// tcl commands
static tcl::Status tcl_command_hard_quit(tcl::Interp &interp, tcl::vector<tcl::string> &argv,
                                         tcl::ProcPrivdata *privdata) {
  auto *pd = static_cast<TclEditorPrivdata *>(privdata);
  pd->editor->running = false;
  return tcl::S_OK;
}

static tcl::Status tcl_command_quit(tcl::Interp &interp, tcl::vector<tcl::string> &argv,
                                    tcl::ProcPrivdata *privdata) {
  auto *pd = static_cast<TclEditorPrivdata *>(privdata);
  if (pd->editor->dirty > 0) {
    interp.result = "file has changes, use q! to quit";
    return tcl::S_ERR;
  }
  return tcl_command_hard_quit(interp, argv, privdata);
}

static tcl::Status tcl_command_write(tcl::Interp &interp, tcl::vector<tcl::string> &argv,
                                     tcl::ProcPrivdata *privdata) {
  auto *pd = static_cast<TclEditorPrivdata *>(privdata);
  Editor &e = *pd->editor;

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
  e.message_set("file written");
  return tcl::S_OK;
}

namespace tevl {

void tevl_main(Backend *be_, Editor *editor, tcl::Interp *interp, ou::string *file_path) {
  // Set up editor's runtime pointers
  editor->be = be_;
  editor->interp = interp;
  editor->running = true;

  // Create privdata for tcl commands
  TclEditorPrivdata privdata(editor);

  // Register tcl commands if interp is provided
  if (interp) {
    tcl::register_core_commands(*interp);

    interp->register_command("q", tcl_command_quit, &privdata);
    interp->register_command("q!", tcl_command_hard_quit, &privdata);
    interp->register_command("quit", tcl_command_quit, &privdata);
    interp->register_command("quit!", tcl_command_hard_quit, &privdata);

    interp->register_command("write", tcl_command_write, &privdata);
    interp->register_command("w", tcl_command_write, &privdata);
  }

  be_->error_msg = default_error_msg;
  if (be_->setup() != EditorErr::NONE) {
    oprintf("failed to setup be: %s\n", be_->error_msg);
    return;
  }

  ou::string tilde("~");

  if (file_path) {
    ou::File file(file_path->c_str());
    editor->file_name = file_path->c_str();
    ErrorCode err = file.open();
    if (err != ErrorCode::NONE) {
      oprintf("failed to open file %s: %d\n", file_path->c_str(), err);
      return;
    }

    // Read file contents and split into lines
    ou::string content;
    err = file.read_all(content);
    if (err != ErrorCode::NONE) {
      oprintf("failed to read file %s: %d\n", file_path->c_str(), err);
      return;
    }

    // Split content by newlines
    ou::string current_line;
    for (size_t i = 0; i < content.length(); i++) {
      if (content[i] == '\n') {
        editor->file_lines.push_back(current_line);
        current_line.clear();
      } else {
        current_line.push_back(content[i]);
      }
    }
    // Add last line if file doesn't end with newline
    if (!current_line.empty() || content.empty()) {
      editor->file_lines.push_back(current_line);
    }
  }

  while (editor->running) {
    // Check if we should process this frame (for cooperative scheduling)
    if (!be_->begin_frame()) {
      be_->yield();
      continue;
    }

    // Handle scroll
    auto ws = be_->getWindowSize();
    editor->scroll();

    // Handle message clear
    editor->message_clear();

    // Render individual rows
    editor->screenResetLines();

    editor->generate_status_line();

    for (int y = 0; y < ws.y; y++) {
      intptr_t file_row = y + editor->row_offset;
      if (file_row < editor->file_lines.size()) {
        intptr_t len = editor->file_lines[file_row].length() - editor->col_offset;
        if (len < 0) {
          len = 0;
        }
        if (len > ws.x) {
          len = ws.x;
        }
        editor->screenPutLine(y, editor->file_lines[file_row].substr(editor->col_offset, len), len);
      } else {
        editor->screenPutLine(y, tilde);
      }
    }

    be_->render(*editor);
    // Handle user input
    editor->process_key_press();

    be_->end_frame();
    be_->yield();
  }

  be_->teardown();

  if (be_->error_msg != default_error_msg) {
    oprintf("error: %s\n", be_->error_msg);
  }

  be_->clear();
  return;
}

} // namespace tevl
