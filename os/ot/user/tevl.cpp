// tevl.cpp - TEVL text editor core implementation
#include "ot/user/tevl.hpp"
#include "ot/common.h"
#include "ot/shared/result.hpp"
#include "ot/user/string.hpp"

static bool running = true;
static const char *default_error_msg = "no error message set";
static tevl::Backend *be = nullptr;

#define CTRL_KEY(k) ((k) & 0x1f)

ou::string buffer;

using namespace tevl;

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
    if (e.cx > 0)
      e.cx--;
  } else if (k.ext == ExtendedKey::ARROW_RIGHT) {
    e.cx++;
  } else if (k.ext == ExtendedKey::ARROW_UP) {
    if (e.cy > 0)
      e.cy--;
  } else if (k.ext == ExtendedKey::ARROW_DOWN) {
    e.cy++;
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

  ou::string tevl_welcome("\033[38;5;12mtevl\033[0m (text editor, vi-like [aspirational])");
  ou::string tevl_padded_welcome;

  Coord previous_ws;

  while (running) {
    // be->refresh();

    auto ws = be->getWindowSize();
    int center_x = (ws.x - tevl_welcome.length()) / 2;

    // Render the padded welcome
    if (ws.x != previous_ws.x || ws.y != previous_ws.y) {
      tevl_padded_welcome.clear();
      tevl_padded_welcome.ensure_capacity(ws.x + tevl_welcome.length());
      tevl_padded_welcome.append(tilde);
      for (int i = 0; i < center_x; i++) {
        tevl_padded_welcome.append(" ");
      }
      tevl_padded_welcome.append(tevl_welcome);
    }

    e.resetLines();

    int center_y = ws.y / 2;
    for (int y = 0; y < ws.y; y++) {
      e.putLine(y, tilde);
    }
    e.putLine(center_y, tevl_padded_welcome);
    be->render(e.cx, e.cy, e.lines);
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