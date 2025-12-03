// prog-typedemo.cpp - Keyboard typing demo
#include "ot/lib/frame-manager.hpp"
#include "ot/lib/app-framework.hpp"
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
static const int CHAR_WIDTH = 11; // Approximate for monospace at BODY_SIZE
static const int LINE_HEIGHT = 22;

// Display buffer
static char display_buffer[MAX_CHARS];
static int buffer_pos = 0;
static int idle_frames = 0;

// Map key scan code to ASCII character (simple US-QWERTY layout)
char scancode_to_ascii(uint16_t code) {
  // Numbers (KEY_1 = 2 to KEY_9 = 10, KEY_0 = 11)
  if (code >= KEY_1 && code <= KEY_9) {
    return '1' + (code - KEY_1);
  }
  if (code == KEY_0) {
    return '0';
  }

  // Top row letters (Q-P)
  if (code == KEY_Q)
    return 'q';
  if (code == KEY_W)
    return 'w';
  if (code == KEY_E)
    return 'e';
  if (code == KEY_R)
    return 'r';
  if (code == KEY_T)
    return 't';
  if (code == KEY_Y)
    return 'y';
  if (code == KEY_U)
    return 'u';
  if (code == KEY_I)
    return 'i';
  if (code == KEY_O)
    return 'o';
  if (code == KEY_P)
    return 'p';

  // Home row letters (A-L)
  if (code == KEY_A)
    return 'a';
  if (code == KEY_S)
    return 's';
  if (code == KEY_D)
    return 'd';
  if (code == KEY_F)
    return 'f';
  if (code == KEY_G)
    return 'g';
  if (code == KEY_H)
    return 'h';
  if (code == KEY_J)
    return 'j';
  if (code == KEY_K)
    return 'k';
  if (code == KEY_L)
    return 'l';

  // Bottom row letters (Z-M)
  if (code == KEY_Z)
    return 'z';
  if (code == KEY_X)
    return 'x';
  if (code == KEY_C)
    return 'c';
  if (code == KEY_V)
    return 'v';
  if (code == KEY_B)
    return 'b';
  if (code == KEY_N)
    return 'n';
  if (code == KEY_M)
    return 'm';

  // Special keys
  if (code == KEY_SPACE)
    return ' ';
  if (code == KEY_ENTER)
    return '\n';

  return 0; // Unmapped key
}

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

  // Map to ASCII
  char ch = scancode_to_ascii(code);
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
      gfx.draw_ttf_text(20, 20, "KEYBOARD TYPING DEMO", 0xFFEEEEEE, TITLE_SIZE);
      gfx.draw_ttf_text(20, 55, "Type to see characters appear. Backspace to delete. 5s idle clears.", 0xFFCCCCCC,
                        SUBTITLE_SIZE);

      // Draw display buffer as text (simple word wrap)
      int x = 20;
      int y = 90;
      for (int i = 0; i < buffer_pos; i++) {
        char ch = display_buffer[i];

        if (ch == '\n') {
          // Newline
          x = 20;
          y += LINE_HEIGHT;
        } else {
          // Draw character
          char str[2] = {ch, 0};
          gfx.draw_ttf_text(x, y, str, 0xFFFFFFFF, BODY_SIZE);
          x += CHAR_WIDTH;

          // Word wrap
          if (x + CHAR_WIDTH >= width - 20) {
            x = 20;
            y += LINE_HEIGHT;
          }
        }

        // Stop if we run out of vertical space
        if (y + BODY_SIZE >= height - 20) {
          break;
        }
      }

      // Draw cursor
      if ((idle_frames / 30) % 2 == 0) {
        gfx.draw_ttf_text(x, y, "_", 0xFFFFFF00, BODY_SIZE);
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
