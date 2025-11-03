// tevl.cpp - TEVL text editor core implementation
#include "ot/user/tevl.h"
#include "ot/common.h"
#include "ot/shared/result.hpp"

static bool running = true;
static const char *default_error_msg = "no error message set";
static tevl::Backend *be = nullptr;

#define CTRL_KEY(k) ((k) & 0x1f)

struct Editor {
  int cx, cy;
} e;

void process_key_press() {
  auto res = be->readKey();

  if (res.is_err()) {
    oprintf("failed to read key errcode=%d\n", res.error());
    return;
  }
  char c = res.value();

  if (c == 'q') {
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

  while (running) {

    be->refresh();
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