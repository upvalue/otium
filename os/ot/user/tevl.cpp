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

enum EditorErr {
  TERM_READ_KEY_FAILED,
};

struct TerminalBackend : Backend {
  virtual Result<char, EditorErr> readKey() override {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) == -1) {
      return Result<char, EditorErr>::err(TERM_READ_KEY_FAILED);
    }
    return Result<char, EditorErr>::ok(c);
  }
};

void disable_raw_mode() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

void enable_raw_mode() {
  // enable raw mode
  tcgetattr(STDIN_FILENO, &orig_termios);
  struct termios raw = orig_termios;
  // Doesn't seem necessary on OSX?
  // raw.c_iflag &= ~(IXON);
  // ICRNL - disable ctrl-m being read as carriage return
  raw.c_iflag &= ~(ICRNL);
  // Disable output processing
  raw.c_oflag &= ~(OPOST);
  // raw.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
  raw.c_lflag &= ~(ECHO | ICANON | ISIG);

  // READ settings
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;

  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
    FATAL("tcsetattr failed");
  };
}

void tevl_main() {
  enable_raw_mode();

  // main editor loop
  while (running) {
    char c = 0;
    read(STDIN_FILENO, &c, 1);

    if (c == 'q') {
      break;
    }

    /*
    if (iscntrl(c)) {
      printf("%d\r\n", c);
    } else {
      printf("%d ('%c')\r\n", c, c);
    }
      */

    if (c == CTRL_KEY('q')) {
      break;
    }
  }

  disable_raw_mode();
  return;
}
