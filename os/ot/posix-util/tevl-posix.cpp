// tevl with posix terminal backend
#include "ot/user/tevl.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

// Provide ou memory allocation functions using stdlib
extern "C" {
void *ou_malloc(size_t size) { return malloc(size); }
void ou_free(void *ptr) { free(ptr); }
void *ou_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
}

// write some terminal control stuff
#define tctrl(x)                                                                                                       \
  do {                                                                                                                 \
    write(STDOUT_FILENO, x, sizeof(x) - 1);                                                                            \
  } while (0)

#define CTRL_KEY(c) ((c) & 0x1f)

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

  virtual Result<Coord, EditorErr> getCursorPosition() override {
    int rows, cols;
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4)
      return Result<Coord, EditorErr>::err(EditorErr::FATAL_TERM_GET_CURSOR_POSITION_FAILED);

    while (i < sizeof(buf) - 1) {
      if (read(STDIN_FILENO, &buf[i], 1) != 1)
        break;
      if (buf[i] == 'R')
        break;
      i++;
    }
    buf[i] = '\0';

    if (buf[0] != '\x1b' || buf[1] != '[')
      return Result<Coord, EditorErr>::err(EditorErr::FATAL_TERM_GET_CURSOR_POSITION_FAILED);
    if (sscanf(&buf[2], "%d;%d", &rows, &cols) != 2)
      return Result<Coord, EditorErr>::err(EditorErr::FATAL_TERM_GET_CURSOR_POSITION_FAILED);

    return Result<Coord, EditorErr>::ok(Coord{cols, rows});
  }

  virtual Result<Key, EditorErr> readKey() override {
    char c = 0;
    if (read(STDIN_FILENO, &c, 1) == -1) {
      return Result<Key, EditorErr>::err(EditorErr::FATAL_TERM_READ_KEY_FAILED);
    }

    // Translate key
    Key key;

    key.c = c;

    if (c == '\x1b') {
      char seq[3];

      if (read(STDIN_FILENO, &seq[0], 1) != 1) {
        key.c = '\x1b';
        return Result<Key, EditorErr>::ok(key);
      }
      if (read(STDIN_FILENO, &seq[1], 1) != 1) {
        key.c = '\x1b';
        return Result<Key, EditorErr>::ok(key);
      }

      if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
          if (read(STDIN_FILENO, &seq[2], 1) != 1) {
            key.c = '\x1b';
            return Result<Key, EditorErr>::ok(key);
          }
          if (seq[2] == '~') {
            switch (seq[1]) {
            case '1':
              key.ext = HOME_KEY;
              return Result<Key, EditorErr>::ok(key);
            case '3':
              key.ext = DEL_KEY;
              return Result<Key, EditorErr>::ok(key);
            case '4':
              key.ext = END_KEY;
              return Result<Key, EditorErr>::ok(key);
            case '5':
              key.ext = PAGE_UP;
              return Result<Key, EditorErr>::ok(key);
            case '6':
              key.ext = PAGE_DOWN;
              return Result<Key, EditorErr>::ok(key);
            case '7':
              key.ext = HOME_KEY;
              return Result<Key, EditorErr>::ok(key);
            case '8':
              key.ext = END_KEY;
              return Result<Key, EditorErr>::ok(key);
            }
          }
        } else {
          switch (seq[1]) {
          case 'A':
            key.ext = ARROW_UP;
            return Result<Key, EditorErr>::ok(key);
          case 'B':
            key.ext = ARROW_DOWN;
            return Result<Key, EditorErr>::ok(key);
          case 'C':
            key.ext = ARROW_RIGHT;
            return Result<Key, EditorErr>::ok(key);
          case 'D':
            key.ext = ARROW_LEFT;
            return Result<Key, EditorErr>::ok(key);
          case 'H':
            key.ext = HOME_KEY;
            return Result<Key, EditorErr>::ok(key);
          case 'F':
            key.ext = END_KEY;
            return Result<Key, EditorErr>::ok(key);
          }
        }
      } else if (seq[0] == 'O') {
        switch (seq[1]) {
        case 'H':
          key.ext = HOME_KEY;
          return Result<Key, EditorErr>::ok(key);
        case 'F':
          key.ext = END_KEY;
          return Result<Key, EditorErr>::ok(key);
        }
      }
    }

    // TODO: bugs might lurk here
    for (int i = 'a'; i <= 'z'; i++) {
      if (c == CTRL_KEY(i)) {
        key.c = i;
        key.ctrl = true;
        break;
      }
    }

    return Result<Key, EditorErr>::ok(key);
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

  virtual void render(const ou::vector<ou::string> &lines) override {
    Coord ws = getWindowSize();
    for (int i = 0; i < ws.y; i++) {
      if (i < lines.size()) {
        write(STDOUT_FILENO, lines[i].data(), lines[i].length());
        write(STDOUT_FILENO, "\r\n", 2);
      } else {
        write(STDOUT_FILENO, "\n", 1);
      }
    }
  }
};

} // namespace tevl

int main(int argc, char *argv[]) {
  tevl::PosixTermBackend posix_term_backend;
  tevl::tevl_main(&posix_term_backend);
  return 0;
}
