// tevl.cpp - TEVL text editor core implementation
#include "ot/user/tevl.h"
#include "ot/common.h"
#include "ot/shared/result.hpp"
#include "ot/user/string.hpp"

static bool running = true;
static const char *default_error_msg = "no error message set";
static tevl::Backend *be = nullptr;

#define CTRL_KEY(k) ((k) & 0x1f)

ou::string buffer;

using namespace tevl;

struct Editor {
  Editor() : cx(0), cy(0) {}
  int cx, cy;
  /** Lines to render; note that this is only roughly the height of the screen */
  ou::vector<ou::string> lines;

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

} e;

void process_key_press() {
  auto res = be->readKey();

  if (res.is_err()) {
    oprintf("failed to read key errcode=%d\n", res.error());
    return;
  }

  Key k = res.value();

  if (k.ctrl && k.c == 'q') {
    running = false;
  }
}

namespace tevl {

void tevl_main(Backend *be_) {
  be = be_;
  be->error_msg = default_error_msg;
  if (be->setup() != EditorErr::NONE) {
    oprintf("failed to setup be: %s\n", be->error_msg);
    return;
  }

  ou::string tilde("~");

  ou::string tevl_welcome("tevl (text editor, vi-like [aspirational])");
  ou::string tevl_padded_welcome;
  while (running) {
    be->refresh();

    auto dim = be->getWindowSize();
    int center_x = (dim.x - tevl_welcome.length()) / 2;

    tevl_padded_welcome.clear();
    tevl_padded_welcome.ensure_capacity(dim.x + tevl_welcome.length());
    tevl_padded_welcome.append(tilde);
    for (int i = 0; i < center_x; i++) {
      tevl_padded_welcome.append(" ");
    }
    tevl_padded_welcome.append(tevl_welcome);

    e.resetLines();

    int center_y = dim.y / 2;
    for (int y = 0; y < dim.y; y++) {
      e.putLine(y, tilde);
    }
    e.putLine(center_y, tevl_padded_welcome);
    be->render(e.lines);
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