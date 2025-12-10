// tevl with termbox2 terminal backend
#define TB_IMPL
#include "vendor/termbox2/termbox2.h"

#include "ot/user/tcl.hpp"
#include "ot/user/tevl.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

// Provide ou memory allocation functions using stdlib
void *ou_malloc(size_t size) { return malloc(size); }
void ou_free(void *ptr) { free(ptr); }
void *ou_realloc(void *ptr, size_t size) { return realloc(ptr, size); }

namespace tevl {

struct TermboxBackend : Backend {
  int debug_fd;

  TermboxBackend() : debug_fd(-1) {
    // Open debug file in append mode
    debug_fd = open("/tmp/tevl-debug.txt", O_WRONLY | O_CREAT | O_APPEND, 0644);
  }

  ~TermboxBackend() {
    if (debug_fd != -1) {
      close(debug_fd);
    }
  }

  virtual EditorErr setup() override {
    int rv = tb_init();
    if (rv != TB_OK) {
      error_msg = tb_strerror(rv);
      return EditorErr::FATAL_TERM_TCSETATTR_FAILED;
    }
    return EditorErr::NONE;
  }

  virtual void teardown() override { tb_shutdown(); }

  virtual void refresh() override { tb_present(); }

  virtual void clear() override { tb_clear(); }

  virtual Coord getWindowSize() override {
    // Reserve 2 lines for status and message/command
    return Coord{tb_width(), tb_height() - 2};
  }

  Key translateKey(struct tb_event &ev) {
    Key key;

    // Handle extended keys
    switch (ev.key) {
    case TB_KEY_ARROW_UP:
      key.ext = ExtendedKey::ARROW_UP;
      return key;
    case TB_KEY_ARROW_DOWN:
      key.ext = ExtendedKey::ARROW_DOWN;
      return key;
    case TB_KEY_ARROW_LEFT:
      key.ext = ExtendedKey::ARROW_LEFT;
      return key;
    case TB_KEY_ARROW_RIGHT:
      key.ext = ExtendedKey::ARROW_RIGHT;
      return key;
    case TB_KEY_HOME:
      key.ext = ExtendedKey::HOME_KEY;
      return key;
    case TB_KEY_END:
      key.ext = ExtendedKey::END_KEY;
      return key;
    case TB_KEY_PGUP:
      key.ext = ExtendedKey::PAGE_UP;
      return key;
    case TB_KEY_PGDN:
      key.ext = ExtendedKey::PAGE_DOWN;
      return key;
    case TB_KEY_DELETE:
      key.ext = ExtendedKey::DEL_KEY;
      return key;
    case TB_KEY_BACKSPACE:
    case TB_KEY_BACKSPACE2:
      key.ext = ExtendedKey::BACKSPACE_KEY;
      return key;
    case TB_KEY_ENTER:
      key.ext = ExtendedKey::ENTER_KEY;
      return key;
    case TB_KEY_ESC:
      key.ext = ExtendedKey::ESC_KEY;
      return key;
    }

    // Handle ctrl keys
    if (ev.key >= TB_KEY_CTRL_A && ev.key <= TB_KEY_CTRL_Z) {
      key.c = 'a' + (ev.key - TB_KEY_CTRL_A);
      key.ctrl = true;
      return key;
    }

    // Regular character
    if (ev.ch != 0) {
      key.c = (char)ev.ch;
    }

    return key;
  }

  virtual Result<Key, EditorErr> readKey() override {
    struct tb_event ev;
    int ret = tb_peek_event(&ev, 10); // 10ms timeout for responsiveness

    if (ret == TB_OK && ev.type == TB_EVENT_KEY) {
      return Result<Key, EditorErr>::ok(translateKey(ev));
    }

    // No key available - return empty key
    return Result<Key, EditorErr>::ok(Key{});
  }

  void drawString(int x, int y, const ou::string &str, uintattr_t fg, uintattr_t bg) {
    for (size_t i = 0; i < str.length() && x + (int)i < tb_width(); i++) {
      tb_set_cell(x + i, y, str[i], fg, bg);
    }
  }

  void drawStringWithClear(int x, int y, const ou::string &str, uintattr_t fg, uintattr_t bg) {
    int width = tb_width();
    size_t i = 0;

    // Draw the string
    for (; i < str.length() && x + (int)i < width; i++) {
      tb_set_cell(x + i, y, str[i], fg, bg);
    }

    // Clear the rest of the line
    for (; x + (int)i < width; i++) {
      tb_set_cell(x + i, y, ' ', fg, bg);
    }
  }

  virtual void render(const Editor &ed) override {
    tb_clear();
    Coord ws = getWindowSize();

    // Draw file lines
    for (int y = 0; y < ws.y; y++) {
      if (y < (int)ed.render_lines.size()) {
        drawStringWithClear(0, y, ed.render_lines[y], TB_WHITE, TB_DEFAULT);
      } else {
        // Clear empty lines
        for (int x = 0; x < ws.x; x++) {
          tb_set_cell(x, y, ' ', TB_WHITE, TB_DEFAULT);
        }
      }
    }

    // Draw status line (inverted colors)
    int status_y = ws.y;
    drawStringWithClear(0, status_y, ed.status_line, TB_BLACK, TB_WHITE);

    // Draw message/command line
    int message_y = ws.y + 1;
    if (!ed.message_line.empty()) {
      drawStringWithClear(0, message_y, ed.message_line, TB_WHITE, TB_DEFAULT);
    } else if (ed.mode == EditorMode::COMMND) {
      tb_set_cell(0, message_y, ';', TB_WHITE, TB_DEFAULT);
      drawString(1, message_y, ed.command_line, TB_WHITE, TB_DEFAULT);
      // Clear rest of line
      for (int x = 1 + ed.command_line.length(); x < ws.x; x++) {
        tb_set_cell(x, message_y, ' ', TB_WHITE, TB_DEFAULT);
      }
    } else {
      // Clear message line
      for (int x = 0; x < ws.x; x++) {
        tb_set_cell(x, message_y, ' ', TB_WHITE, TB_DEFAULT);
      }
    }

    // Position cursor
    int cursor_x = ed.rx - ed.col_offset;
    int cursor_y = ed.cy - ed.row_offset;
    tb_set_cursor(cursor_x, cursor_y);

    tb_present();
  }

  virtual void debug_print(const ou::string &msg) override {
    if (debug_fd == -1)
      return;

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
  tevl::Editor editor;
  tcl::Interp interp;
  tevl::TermboxBackend termbox_backend;

  ou::string *file_path = nullptr;
  if (argc > 1) {
    file_path = new ou::string(argv[1]);
  }

  tevl::tevl_main(&termbox_backend, &editor, &interp, file_path);

  if (file_path) {
    delete file_path;
  }
  return 0;
}
