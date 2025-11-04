// tevl with posix terminal backend
#include "ot/user/tevl.hpp"

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

// Provide ou memory allocation functions using stdlib
extern "C" {
void *ou_malloc(size_t size) { return malloc(size); }
void ou_free(void *ptr) { free(ptr); }
void *ou_realloc(void *ptr, size_t size) { return realloc(ptr, size); }
}

// write a terminal control sequence with statically known size
#define tctrl(x)                                                                                                       \
  do {                                                                                                                 \
    write(STDOUT_FILENO, x, sizeof(x) - 1);                                                                            \
  } while (0)

#define CTRL_KEY(c) ((c) & 0x1f)

ou::string output_buffer;

namespace tevl {

struct PosixTermBackend : Backend {
  struct termios orig_termios, raw;
  int debug_fd; // File descriptor for debug output

  PosixTermBackend() : debug_fd(-1) {
    tcgetattr(STDIN_FILENO, &orig_termios);
    raw = orig_termios;
    raw.c_iflag &= ~(ICRNL);
    raw.c_oflag &= ~(OPOST);
    raw.c_lflag &= ~(ECHO | ICANON | ISIG);

    // Open debug file in append mode
    debug_fd = open("/tmp/tevl-debug.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
  }

  ~PosixTermBackend() {
    if (debug_fd != -1) {
      close(debug_fd);
    }
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

  virtual void render(int cx, int cy, const ou::vector<ou::string> &lines) override {
    // Hide cursor
    tctrl("\x1b[?25l]");
    // Move cursor to top left for drawing
    tctrl("\x1b[H");
    Coord ws = getWindowSize();
    output_buffer.clear();
    for (int i = 0; i < ws.y; i++) {
      if (i < lines.size()) {
        output_buffer.append(lines[i]);
        if (i != ws.y - 1) {
          output_buffer.append("\x1b[K");
          output_buffer.append("\r\n");
        }
      }
    }

    // Reset cursor position at the end
    char buf[32];
    // terminal is 1-indexed
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", cy + 1, cx + 1);
    output_buffer.append(buf);

    write(STDOUT_FILENO, output_buffer.data(), output_buffer.length());

    // Show cursor
    tctrl("\x1b[?25h");
  }

  virtual void debug_print(const ou::string &msg) override {
    if (debug_fd == -1)
      return; // Debug file not open

    // Add timestamp
    time_t rawtime;
    struct tm *timeinfo;
    char timestamp[32];
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime(timestamp, sizeof(timestamp), "[%Y-%m-%d %H:%M:%S] ", timeinfo);

    // Write timestamp and message
    write(debug_fd, timestamp, strlen(timestamp));
    write(debug_fd, msg.data(), msg.length());
    write(debug_fd, "\n", 1);

    // Flush for immediate output
    fsync(debug_fd);
  }
};

} // namespace tevl

int main(int argc, char *argv[]) {
  tevl::Editor e;
  tevl::PosixTermBackend posix_term_backend;
  ou::string *file_path = nullptr;
  if (argc > 1) {
    file_path = new ou::string(argv[1]);
  }
  tevl::tevl_main(&posix_term_backend, file_path);
  if (file_path) {
    delete file_path;
  }
  return 0;
}
