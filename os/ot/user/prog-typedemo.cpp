// prog-typedemo.cpp - Keyboard typing demo
#include "ot/lib/app-framework.hpp"
#include "ot/lib/frame-manager.hpp"
#include "ot/lib/keyboard-utils.hpp"
#include "ot/user/gen/graphics-client.hpp"
#include "ot/user/gen/keyboard-client.hpp"
#include "ot/user/keyboard/backend.hpp"
#include "ot/user/local-storage.hpp"
#include "ot/user/prog.h"
#include "ot/user/user.hpp"

// Constants
static const int MAX_CHARS = 256;
static const int IDLE_TIMEOUT_FRAMES = 300; // 5 seconds at 60 FPS

// Font sizes (pixels)
static const int TITLE_SIZE = 28;
static const int SUBTITLE_SIZE = 16;
static const int BODY_SIZE = 18;
static const int WRAP_WIDTH = 300;     // Text wrapping width
static const int WRAP_LINE_OFFSET = 5; // Offset for wrap indicator line
static const int TEXT_START_X = 20;
static const int TEXT_START_Y = 90;

// Display buffer
static char display_buffer[MAX_CHARS];
static int buffer_pos = 0;
static int idle_frames = 0;

void handle_key_event(uint16_t code, uint8_t flags) {
  // Only process key press events (not release)
  if (!(flags & KEY_FLAG_PRESSED)) {
    return;
  }

  // Backspace
  if (code == KEY_BACKSPACE) {
    if (buffer_pos > 0) {
      buffer_pos--;
    }
    idle_frames = 0;
    return;
  }

  // Map to ASCII (with shift support)
  bool shift = (flags & KEY_FLAG_SHIFT) != 0;
  char ch = keyboard::scancode_to_ascii(code, shift);
  oprintf("TYPEDEMO: char: %c %d\n", ch, code);
  if (ch && buffer_pos < MAX_CHARS - 1) {
    display_buffer[buffer_pos++] = ch;
    idle_frames = 0;
  }
}

struct TypeDemoStorage : public LocalStorage {
  // Need ~20 pages for TTF font rendering (glyph buffers, etc.)
  TypeDemoStorage() { process_storage_init(20); }
};

void typedemo_main() {
  void *storage_page = ou_get_storage().as_ptr();
  TypeDemoStorage *s = new (storage_page) TypeDemoStorage();

  oprintf("TYPEDEMO: Starting keyboard typing demo\n");

  // Yield to let graphics and keyboard drivers initialize
  ou_yield();

  // Look up graphics driver
  Pid gfx_pid = ou_proc_lookup("graphics");
  if (gfx_pid == PID_NONE) {
    oprintf("TYPEDEMO: Failed to find graphics driver\n");
    ou_exit();
  }

  // Look up keyboard driver
  Pid kbd_pid = ou_proc_lookup("keyboard");
  if (kbd_pid == PID_NONE) {
    oprintf("TYPEDEMO: Failed to find keyboard driver\n");
    ou_exit();
  }

  GraphicsClient gfx_client(gfx_pid);
  KeyboardClient kbd_client(kbd_pid);

  // Get framebuffer info
  auto fb_result = gfx_client.get_framebuffer();
  if (fb_result.is_err()) {
    oprintf("TYPEDEMO: Failed to get framebuffer: %d\n", fb_result.error());
    ou_exit();
  }

  auto fb_info = fb_result.value();
  uint32_t *fb = (uint32_t *)fb_info.fb_ptr;
  int width = (int)fb_info.width;
  int height = (int)fb_info.height;

  oprintf("TYPEDEMO: Framebuffer %dx%d\n", width, height);

  // Create graphics utility wrapper
  app::Framework gfx(fb, width, height);

  // Initialize TTF font
  auto ttf_result = gfx.init_ttf();
  if (ttf_result.is_err()) {
    oprintf("TYPEDEMO: Failed to init TTF font: %s\n", error_code_to_string(ttf_result.error()));
    ou_exit();
  }
  oprintf("TYPEDEMO: TTF font initialized\n");

  // Initialize display buffer
  memset(display_buffer, 0, MAX_CHARS);
  buffer_pos = 0;
  idle_frames = 0;

  // Run demo at 60 FPS
  graphics::FrameManager fm(60);

  oprintf("TYPEDEMO: Running (type to see characters, backspace to delete, "
          "5s idle clears)\n");

  while (true) {
    if (fm.begin_frame()) {
      // Poll for keyboard events
      auto key_result = kbd_client.poll_key();
      if (key_result.is_ok()) {
        auto key_data = key_result.value();
        if (key_data.has_key) {
          handle_key_event(key_data.code, key_data.flags);
        }
      }

      // Check idle timeout
      idle_frames++;
      if (idle_frames >= IDLE_TIMEOUT_FRAMES) {
        // Clear buffer
        memset(display_buffer, 0, MAX_CHARS);
        buffer_pos = 0;
        idle_frames = 0;
      }

      // Clear screen to dark blue background
      gfx.clear(0xFF1A1A2E);

      // Draw title and instructions
      gfx.draw_ttf_text(TEXT_START_X, 20, "KEYBOARD TYPING DEMO", 0xFFEEEEEE, TITLE_SIZE);
      gfx.draw_ttf_text(TEXT_START_X, 55, "Type to see characters appear. Backspace to delete. 5s idle clears.",
                        0xFFCCCCCC, SUBTITLE_SIZE);

      // Draw red wrap indicator line (5px after the wrap boundary)
      int wrap_line_x = TEXT_START_X + WRAP_WIDTH + WRAP_LINE_OFFSET;
      gfx.draw_vline(wrap_line_x, TEXT_START_Y, height - TEXT_START_Y - 20, 0xFF4444AA); // Red line

      // Draw display buffer as text with wrapping
      display_buffer[buffer_pos] = '\0'; // Null-terminate for drawing
      auto wrap_result =
          gfx.draw_ttf_text_wrapped(TEXT_START_X, TEXT_START_Y, WRAP_WIDTH, display_buffer, 0xFFFFFFFF, BODY_SIZE);

      // Draw cursor at end of text
      if ((idle_frames / 30) % 2 == 0) {
        int cursor_y = TEXT_START_Y;
        if (wrap_result.is_ok()) {
          cursor_y = TEXT_START_Y + wrap_result.value() - BODY_SIZE - 2; // Adjust to last line
        }
        // Measure last line to find cursor x position
        int cursor_x = TEXT_START_X;
        // Find start of last line
        int last_line_start = 0;
        for (int i = buffer_pos - 1; i >= 0; i--) {
          if (display_buffer[i] == '\n') {
            last_line_start = i + 1;
            break;
          }
        }
        auto measure_result = gfx.measure_ttf_text(display_buffer + last_line_start, BODY_SIZE);
        if (measure_result.is_ok()) {
          cursor_x = TEXT_START_X + (measure_result.value() % WRAP_WIDTH);
        }
        gfx.draw_ttf_text(cursor_x, cursor_y, "_", 0xFFFFFF00, BODY_SIZE);
      }

      // Flush to display
      gfx_client.flush();
      fm.end_frame();
    }

    // Always yield to cooperate with other processes
    ou_yield();
  }

  ou_exit();
}
