// tevl.cpp - TEVL text editor core implementation
#include "ot/user/tevl.h"
#include "ot/common.h"
#include "ot/shared/result.hpp"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static bool running = true;

static struct termios orig_termios, raw;

#define FATAL(...)                                                             \
  do {                                                                         \
    disable_raw_mode();                                                        \
    oprintf("\r\nFATAL: %s\r\n", ##__VA_ARGS__);                               \
    exit(1);                                                                   \
  } while (0)

#define CTRL_KEY(k) ((k) & 0x1f)

void disable_raw_mode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

void enable_raw_mode() {}

namespace tevl {

void tevl_main(Backend *backend) {
  if (backend->setup() != EditorErr::NONE) {
    oprintf("failed to setup backend: %s\n", backend->error_msg);
    return;
  }

  while (running) {
    auto res = backend->readKey();
    if (res.is_err()) {
      oprintf("failed to read key errcode=%d\n", res.error());
      continue;
    }
    char c = res.value();

    if (c == 'q') {
      running = false;
      break;
    }
  }

  backend->teardown();
  return;
}

} // namespace tevl
/*
void tevl_main() {

  enable_raw_mode();

  // main editor loop
  while (running) {
    char c = 0;
    read(STDIN_FILENO, &c, 1);

    if (c == 'q') {
      break;
    }*/

/*
if (iscntrl(c)) {
  printf("%d\r\n", c);
} else {
  printf("%d ('%c')\r\n", c, c);
}
  */

/*if (c == CTRL_KEY('q')) {
  break;
}
}

disable_raw_mode();
return;
}*/
