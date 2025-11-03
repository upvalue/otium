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

  void putLine(int y, const ou::string &line) {
    if (y >= lines.size()) {
      while (lines.size() <= y) {
        lines.push_back(ou::string());
      }
    }
    lines[y] = line;
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
  while (running) {
    be->refresh();

    e.resetLines();

    auto dim = be->getWindowSize();
    for (int y = 0; y < dim.y; y++) {
      e.putLine(y, tilde);
      // e.putLine(y, "~\r\n");
    }
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