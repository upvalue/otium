// tevl with posix terminal backend
#include "ot/user/tevl.h"

#include <ctype.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

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
      return Result<char, EditorErr>::err(
          EditorErr::FATAL_TERM_READ_KEY_FAILED);
    }
    return Result<char, EditorErr>::ok(c);
  }

  virtual EditorErr setup() override {
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
      return EditorErr::FATAL_TERM_TCSETATTR_FAILED;
    }
    return EditorErr::NONE;
  }

  virtual void teardown() override {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
  }
};

} // namespace tevl

int main(int argc, char *argv[]) {
  tevl::PosixTermBackend posix_term_backend;
  tevl::tevl_main(&posix_term_backend);
  return 0;
}
