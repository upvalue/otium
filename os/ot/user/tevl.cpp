// tevl.cpp - TEVL text editor core implementation
#include "ot/user/tevl.hpp"
#include "ot/common.h"
#include "ot/shared/result.hpp"
#include "ot/user/file.hpp"
#include "ot/user/string.hpp"

static bool running = true;
static const char *default_error_msg = "no error message set";
static tevl::Backend *be = nullptr;

#define CTRL_KEY(k) ((k) & 0x1f)

ou::string buffer;

using namespace tevl;
using namespace ou;

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
    be->debug_print("opening file ");
    be->debug_print(*file_path);
    FileErr err = file.open();
    if (err != FileErr::NONE) {
      oprintf("failed to open file %s: %d\n", file_path->c_str(), err);
      return;
    }

    file.forEachLine([](const ou::string &line) {
      be->debug_print("adding line to file: ");
      e.file_lines.push_back(line);
    });
  }

  while (running) {
    // be->refresh();

    auto ws = be->getWindowSize();

    e.screenResetLines();

    for (int y = 0; y < ws.y; y++) {
      if (y < e.file_lines.size()) {
        e.screenPutLine(y, e.file_lines[y]);
      } else {
        e.screenPutLine(y, tilde);
      }
    }

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