// uieditor.cpp - Graphical text editor for the OS
#include "ot/lib/app-framework.hpp"
#include "ot/lib/frame-manager.hpp"
#include "ot/lib/keyboard-utils.hpp"
#include "ot/lib/mpack/mpack-reader.hpp"
#include "ot/user/edit.hpp"
#include "ot/user/gen/graphics-client.hpp"
#include "ot/user/gen/keyboard-client.hpp"
#include "ot/user/keyboard/backend.hpp"
#include "ot/user/local-storage.hpp"
#include "ot/user/prog.h"
#include "ot/user/string.hpp"
#include "ot/user/tcl.hpp"
#include "ot/user/user.hpp"

#include <stdio.h>

// Linux input key codes for navigation keys
#define KEY_HOME_CODE 102
#define KEY_UP_CODE 103
#define KEY_PAGEUP_CODE 104
#define KEY_LEFT_CODE 105
#define KEY_RIGHT_CODE 106
#define KEY_END_CODE 107
#define KEY_DOWN_CODE 108
#define KEY_PAGEDOWN_CODE 109
#define KEY_DELETE_CODE 111

// Font and layout constants
static const int FONT_SIZE = 16;
static const int LINE_HEIGHT = 20;
static const int TEXT_START_X = 10;
static const int TEXT_START_Y = 10;

// Forward declarations
struct GraphicsEditorStorage;

// Graphics backend for the editor
struct GraphicsEditorBackend : edit::Backend {
  GraphicsEditorStorage *storage;
  GraphicsClient *gfx_client;
  KeyboardClient *kbd_client;
  app::Framework *gfx;
  graphics::FrameManager *frame_manager;

  uint32_t *framebuffer;
  int fb_width;
  int fb_height;
  int char_width;

  GraphicsEditorBackend(GraphicsEditorStorage *s) : storage(s) {
    gfx_client = nullptr;
    kbd_client = nullptr;
    gfx = nullptr;
    frame_manager = nullptr;
    framebuffer = nullptr;
    fb_width = 0;
    fb_height = 0;
    char_width = 8; // Will be calculated after TTF init
  }

  edit::Key translateKeyEvent(uint16_t code, uint8_t flags) {
    edit::Key key;
    bool ctrl = (flags & KEY_FLAG_CTRL) != 0;
    bool shift = (flags & KEY_FLAG_SHIFT) != 0;

    // Extended keys (non-printable)
    switch (code) {
    case KEY_UP_CODE:
      key.ext = edit::ExtendedKey::ARROW_UP;
      return key;
    case KEY_DOWN_CODE:
      key.ext = edit::ExtendedKey::ARROW_DOWN;
      return key;
    case KEY_LEFT_CODE:
      key.ext = edit::ExtendedKey::ARROW_LEFT;
      return key;
    case KEY_RIGHT_CODE:
      key.ext = edit::ExtendedKey::ARROW_RIGHT;
      return key;
    case KEY_BACKSPACE:
      key.ext = edit::ExtendedKey::BACKSPACE_KEY;
      return key;
    case KEY_ENTER:
      key.ext = edit::ExtendedKey::ENTER_KEY;
      return key;
    case KEY_ESC:
      key.ext = edit::ExtendedKey::ESC_KEY;
      return key;
    case KEY_HOME_CODE:
      key.ext = edit::ExtendedKey::HOME_KEY;
      return key;
    case KEY_END_CODE:
      key.ext = edit::ExtendedKey::END_KEY;
      return key;
    case KEY_PAGEUP_CODE:
      key.ext = edit::ExtendedKey::PAGE_UP;
      return key;
    case KEY_PAGEDOWN_CODE:
      key.ext = edit::ExtendedKey::PAGE_DOWN;
      return key;
    case KEY_DELETE_CODE:
      key.ext = edit::ExtendedKey::DEL_KEY;
      return key;
    }

    // Ctrl combinations
    if (ctrl) {
      char ch = keyboard::scancode_to_ascii(code, false);
      if (ch >= 'a' && ch <= 'z') {
        key.c = ch;
        key.ctrl = true;
        return key;
      }
    }

    // Regular printable characters
    char ch = keyboard::scancode_to_ascii(code, shift);
    if (ch) {
      key.c = ch;
    }

    return key;
  }

  virtual edit::EditorErr setup() override;
  virtual void teardown() override;
  virtual void refresh() override;
  virtual void clear() override;
  virtual edit::Coord getWindowSize() override;
  virtual Result<edit::Key, edit::EditorErr> readKey() override;
  virtual void render(const edit::Editor &ed) override;
  virtual void debug_print(const ou::string &msg) override;
  virtual bool begin_frame() override;
  virtual void end_frame() override;
  virtual void yield() override;
};

// Storage for the editor process
struct GraphicsEditorStorage : public LocalStorage {
  GraphicsEditorBackend backend;

  // Inline storage for clients to avoid new operator
  GraphicsClient gfx_client_storage;
  KeyboardClient kbd_client_storage;
  app::Framework gfx_storage;
  graphics::FrameManager frame_manager_storage;

  // Pointers to Editor and Interp - constructed after process_storage_init
  // (tcl::Interp constructor allocates memory, so must be after pool init)
  edit::Editor *editor;
  tcl::Interp *interp;

  GraphicsEditorStorage()
      : backend(this), gfx_client_storage(PID_NONE), kbd_client_storage(PID_NONE), gfx_storage(nullptr, 0, 0),
        frame_manager_storage(60), editor(nullptr), interp(nullptr) {
    // Allocate memory for graphics + TTF rendering + file content
    // Need more pages for larger files
    process_storage_init(100);

    // Now that memory pool is initialized, construct Editor and Interp
    editor = ou::ou_new<edit::Editor>();
    interp = ou::ou_new<tcl::Interp>();
  }
};

// Backend method implementations
edit::EditorErr GraphicsEditorBackend::setup() {
  // Look up graphics driver
  Pid gfx_pid = ou_proc_lookup("graphics");
  if (gfx_pid == PID_NONE) {
    error_msg = "Failed to find graphics driver";
    return edit::EditorErr::FATAL_TERM_TCSETATTR_FAILED;
  }

  // Look up keyboard driver
  Pid kbd_pid = ou_proc_lookup("keyboard");
  if (kbd_pid == PID_NONE) {
    error_msg = "Failed to find keyboard driver";
    return edit::EditorErr::FATAL_TERM_TCSETATTR_FAILED;
  }

  // Use inline storage from parent struct instead of new
  storage->gfx_client_storage = GraphicsClient(gfx_pid);
  storage->kbd_client_storage = KeyboardClient(kbd_pid);
  gfx_client = &storage->gfx_client_storage;
  kbd_client = &storage->kbd_client_storage;

  // Register with graphics server
  auto reg_result = gfx_client->register_app("edit");
  if (reg_result.is_err()) {
    error_msg = "Failed to register with graphics driver";
    return edit::EditorErr::FATAL_TERM_TCSETATTR_FAILED;
  }

  // Get framebuffer info
  auto fb_result = gfx_client->get_framebuffer();
  if (fb_result.is_err()) {
    error_msg = "Failed to get framebuffer";
    return edit::EditorErr::FATAL_TERM_TCSETATTR_FAILED;
  }

  auto fb_info = fb_result.value();
  framebuffer = (uint32_t *)fb_info.fb_ptr;
  fb_width = (int)fb_info.width;
  fb_height = (int)fb_info.height;

  // Use inline storage for graphics framework
  storage->gfx_storage = app::Framework(framebuffer, fb_width, fb_height);
  gfx = &storage->gfx_storage;

  // Initialize TTF font
  auto ttf_result = gfx->init_ttf();
  if (ttf_result.is_err()) {
    error_msg = "Failed to init TTF font";
    return edit::EditorErr::FATAL_TERM_TCSETATTR_FAILED;
  }

  // Calculate character width using TTF measurement
  auto measure_result = gfx->measure_ttf_text("M", FONT_SIZE);
  if (measure_result.is_ok()) {
    char_width = measure_result.value();
  }

  // Use inline storage for frame manager
  frame_manager = &storage->frame_manager_storage;

  return edit::EditorErr::NONE;
}

void GraphicsEditorBackend::teardown() {
  // Clean up pointers (storage is in parent struct, no delete needed)
  frame_manager = nullptr;
  gfx = nullptr;
  kbd_client = nullptr;
  gfx_client = nullptr;
}

void GraphicsEditorBackend::refresh() {
  if (gfx_client) {
    gfx_client->flush();
  }
}

void GraphicsEditorBackend::clear() {
  if (gfx) {
    gfx->clear(0xFF1A1A2E);
  }
}

edit::Coord GraphicsEditorBackend::getWindowSize() {
  // Calculate how many characters/lines fit on screen
  // Reserve 2 lines at bottom for status and message
  int cols = (fb_width - TEXT_START_X * 2) / char_width;
  int rows = (fb_height - TEXT_START_Y * 2) / LINE_HEIGHT - 2;
  return edit::Coord{cols, rows};
}

Result<edit::Key, edit::EditorErr> GraphicsEditorBackend::readKey() {
  if (!kbd_client) {
    return Result<edit::Key, edit::EditorErr>::ok(edit::Key{});
  }

  auto key_result = kbd_client->poll_key();
  if (key_result.is_err()) {
    return Result<edit::Key, edit::EditorErr>::ok(edit::Key{});
  }

  auto key_data = key_result.value();
  if (!key_data.has_key) {
    return Result<edit::Key, edit::EditorErr>::ok(edit::Key{});
  }

  // Only process key press events (not releases)
  if (!(key_data.flags & KEY_FLAG_PRESSED)) {
    return Result<edit::Key, edit::EditorErr>::ok(edit::Key{});
  }

  return Result<edit::Key, edit::EditorErr>::ok(translateKeyEvent((uint16_t)key_data.code, (uint8_t)key_data.flags));
}

void GraphicsEditorBackend::render(const edit::Editor &ed) {
  if (!gfx) {
    return;
  }

  // Clear screen to dark blue
  gfx->clear(0xFF1A1A2E);

  edit::Coord ws = getWindowSize();
  int y = TEXT_START_Y;

  // Draw file lines
  for (int i = 0; i < ws.y && i < (int)ed.render_lines.size(); i++) {
    const ou::string &line = ed.render_lines[i];
    if (line.length() > 0) {
      gfx->draw_ttf_text(TEXT_START_X, y, line.c_str(), 0xFFFFFFFF, FONT_SIZE);
    }
    y += LINE_HEIGHT;
  }

  // Draw ~ for empty lines beyond file content
  for (int i = ed.render_lines.size(); i < ws.y; i++) {
    gfx->draw_ttf_text(TEXT_START_X, y, "~", 0xFF666666, FONT_SIZE);
    y += LINE_HEIGHT;
  }

  // Draw status line (inverted: light background, dark text)
  int status_y = TEXT_START_Y + ws.y * LINE_HEIGHT;
  gfx->fill_rect(0, status_y, fb_width, LINE_HEIGHT, 0xFFCCCCCC);
  if (ed.status_line.length() > 0) {
    gfx->draw_ttf_text(TEXT_START_X, status_y, ed.status_line.c_str(), 0xFF1A1A2E, FONT_SIZE);
  }

  // Draw message/command line
  int message_y = status_y + LINE_HEIGHT;
  if (!ed.message_line.empty()) {
    gfx->draw_ttf_text(TEXT_START_X, message_y, ed.message_line.c_str(), 0xFFFFFFFF, FONT_SIZE);
  } else if (ed.mode == edit::EditorMode::COMMND) {
    // Draw command prompt
    char cmd_buffer[256];
    snprintf(cmd_buffer, sizeof(cmd_buffer), ";%s", ed.command_line.c_str());
    gfx->draw_ttf_text(TEXT_START_X, message_y, cmd_buffer, 0xFFFFFFFF, FONT_SIZE);
  }

  // Draw cursor
  int cursor_x = TEXT_START_X + (ed.rx - ed.col_offset) * char_width;
  int cursor_y = TEXT_START_Y + (ed.cy - ed.row_offset) * LINE_HEIGHT;

  // Draw cursor as a block (in normal mode) or underline (in insert mode)
  if (ed.mode == edit::EditorMode::INSERT) {
    // Underline cursor for insert mode
    gfx->fill_rect(cursor_x, cursor_y + LINE_HEIGHT - 2, char_width, 2, 0xFFFFFF00);
  } else {
    // Block cursor for normal/command mode
    gfx->fill_rect(cursor_x, cursor_y, char_width, LINE_HEIGHT, 0x88FFFFFF);
  }

  // Flush to display
  gfx_client->flush();
}

void GraphicsEditorBackend::debug_print(const ou::string &msg) {
  // Use oprintf for debug output
  oprintf("UIEDITOR: %s\n", msg.c_str());
}

bool GraphicsEditorBackend::begin_frame() {
  if (!gfx_client || !frame_manager) {
    return true;
  }

  // Check if we should render (are we the active app?)
  auto should = gfx_client->should_render();
  if (should.is_err()) {
    return false;
  }
  if (should.value() == 0) {
    return false;
  }

  // Check frame timing
  return frame_manager->begin_frame();
}

void GraphicsEditorBackend::end_frame() {
  if (frame_manager) {
    frame_manager->end_frame();
  }
}

void GraphicsEditorBackend::yield() { ou_yield(); }

// Main entry point
void edit_main() {
  void *storage_page = ou_get_storage().as_ptr();
  GraphicsEditorStorage *s = new (storage_page) GraphicsEditorStorage();

  oprintf("EDIT: Starting graphical editor\n");

  // Yield to let drivers initialize
  ou_yield();

  // Get command line arguments
  PageAddr arg_page = ou_get_arg_page();
  MPackReader reader(arg_page.as<char>(), OT_PAGE_SIZE);

  constexpr size_t MAX_ARGS = 8;
  StringView argv[MAX_ARGS];
  size_t argc = 0;

  if (!reader.read_args_map(argv, MAX_ARGS, argc)) {
    oprintf("EDIT: Failed to read arguments\n");
    ou_exit();
  }

  if (argc < 2) {
    oprintf("EDIT: Usage: edit <filename>\n");
    ou_exit();
  }

  // Get file path from arguments
  ou::string file_path(argv[1].ptr, argv[1].len);
  oprintf("EDIT: Opening file: %s\n", file_path.c_str());

  // Run the editor
  edit::edit_run(&s->backend, s->editor, s->interp, &file_path);

  // Clean up graphics registration before exit
  if (s->backend.gfx_client) {
    s->backend.gfx_client->unregister_app();
  }

  oprintf("EDIT: Exiting\n");
  ou_exit();
}
