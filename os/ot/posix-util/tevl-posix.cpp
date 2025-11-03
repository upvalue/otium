// tevl with posix terminal backend
#include "ot/user/tevl.h"

#include <ctype.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// write some terminal control stuff
#define tctrl(x)                                                                                                       \
  do {                                                                                                                 \
    write(STDOUT_FILENO, x, sizeof(x) - 1);                                                                            \
  } while (0)

namespace tevl {

struct PosixTermBackend : Backend {
  struct termios orig_termios, raw;

  PosixTermBackend() {
    tcgetattr(STDIN_FILENO, &orig_termios);
    raw = orig_termios;
    raw.c_iflag &= ~(ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);
  }

  virtual Result<char, EditorErr> readKey() override {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) == -1) {
      return Result<char, EditorErr>::err(EditorErr::FATAL_TERM_READ_KEY_FAILED);
    }
    return Result<char, EditorErr>::ok(c);
  }

  virtual EditorErr setup() override {
    // enable raw mode

    tcgetattr(STDIN_FILENO, &orig_termios);
    raw = orig_termios;
    // Doesn't seem necessary on OSX?
    // raw.c_iflag &= ~(IXON);
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // READ settings
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1;

    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1) {
      return EditorErr::FATAL_TERM_TCSETATTR_FAILED;
    }
    return EditorErr::NONE;
  }

  virtual void teardown() override { tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); }

  virtual void refresh() override { clear(); }

  virtual void clear() override {
    // clear screen
    tctrl("\x1b[2J");
    // move cursor to top left
    tctrl("\x1b[H");
  }

  virtual Coord getWindowSize() override {
    winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1) {
      return Coord{0, 0};
    }
    return Coord{ws.ws_col, ws.ws_row};
  }
};

} // namespace tevl

int main(int argc, char *argv[]) {
  tevl::PosixTermBackend posix_term_backend;
  tevl::tevl_main(&posix_term_backend);
  return 0;
}
