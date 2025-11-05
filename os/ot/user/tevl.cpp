// tevl.cpp - TEVL text editor core implementation
#include "ot/user/tevl.hpp"
#include "ot/common.h"
#include "ot/shared/result.hpp"
#include "ot/user/file.hpp"
#include "ot/user/string.hpp"
#include "ot/user/user.hpp"

static bool running = true;
static const char *default_error_msg = "no error message set";
static tevl::Backend *be = nullptr;
static int TAB_SIZE = 4;

ou::string buffer;

using namespace tevl;
using namespace ou;

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

void process_key_press() {
  auto res = be->readKey();

  if (res.is_err()) {
    oprintf("failed to read key errcode=%d\n", res.error());
    return;
  }

  auto ws = be->getWindowSize();

  Key k = res.value();

  if (k.ctrl && k.c == 'q') {
    running = false;
  } else if (k.ext == ExtendedKey::ARROW_LEFT) {
    if (e.cx != 0) {
      e.cx--;
    } else if (e.cy > 0) {
      // handle user going back onto previous line
      e.cy--;
      e.cx = e.file_lines[e.cy].length();
    }
  } else if (k.ext == ExtendedKey::ARROW_RIGHT) {
    if (e.cx < e.file_lines[e.cy].length()) {
      e.cx++;
    } else if (e.cy < e.file_lines.size() - 1) {
      // handle user going forward onto next line
      e.cy++;
      e.cx = 0;
    }
  } else if (k.ext == ExtendedKey::ARROW_UP) {
    if (e.cy > 0) {
      e.cy--;
    }
  } else if (k.ext == ExtendedKey::ARROW_DOWN) {
    if (e.cy < e.file_lines.size() - 1) {
      e.cy++;
    }
  }

  // Correct cx if it's beyond the end of the current line
  int current_line_len = e.file_lines[e.cy].length();
  if (e.cx > current_line_len) {
    e.cx = current_line_len;
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

  /*
  if (e.cx < e.col_offset) {
    e.col_offset = e.cx;
  }

  if (e.cx >= e.col_offset + ws.x) {
    e.col_offset = e.cx - ws.x + 1;
  }*/
  if (e.rx < e.col_offset) {
    e.col_offset = e.rx;
  }
  if (e.rx >= e.col_offset + ws.x) {
    e.col_offset = e.rx - ws.x + 1;
  }
}

namespace tevl {

void tevl_main(Backend *be_, ou::string *file_path) {
  be = be_;
  be->error_msg = default_error_msg;
  if (be->setup() != EditorErr::NONE) {
    oprintf("failed to setup be: %s\n", be->error_msg);
    return;
  }

  ou::string tilde("~");

  if (file_path) {
    ou::File file(file_path->c_str());
    // be->debug_print("opening file ");
    // be->debug_print(*file_path);
    FileErr err = file.open();
    if (err != FileErr::NONE) {
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

    // Render individual rows
    e.screenResetLines();

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

    // be->render(e.cy - e.row_offset, e.cx, e.lines);
    be->render(e.col_offset, e.rx - e.col_offset, e.cy - e.row_offset, e.render_lines);
    // be->render(e.cy - e.row_offset, e.cx, e.screen_lines);

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