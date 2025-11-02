// tevl.cpp - TEVL text editor core implementation
#include "ot/user/tevl.h"
#include "ot/common.h"

#include <ctype.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

static bool running = true;

static struct termios orig_termios;

void disable_raw_mode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

void tevl_main() {
  // enable raw mode
  tcgetattr(STDIN_FILENO, &orig_termios);
  struct termios raw = orig_termios;
  // Doesn't seem necessary on OSX?
  // raw.c_iflag &= ~(IXON);
  raw.c_lflag &= ~(ECHO | ICANON | ISIG);
  tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);

  // main editor loop
  char c;
  while (running) {
    if (read(STDIN_FILENO, &c, 1) != 1) {
      break;
    }

    if (c == 'q') {
      break;
    }

    if (iscntrl(c)) {
      printf("%d\n", c);
    } else {
      printf("%d ('%c')\n", c, c);
    }
  }

  disable_raw_mode();
  return;
}
