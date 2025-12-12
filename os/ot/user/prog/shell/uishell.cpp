// uishell.cpp - Graphical TCL shell implementation
#include "ot/lib/app-framework.hpp"
#include "ot/lib/frame-manager.hpp"
#include "ot/lib/keyboard-utils.hpp"
#include "ot/lib/mpack/mpack-utils.hpp"
#include "ot/lib/string-view.hpp"
#include "ot/user/gen/graphics-client.hpp"
#include "ot/user/gen/keyboard-client.hpp"
#include "ot/user/gen/tcl-vars.hpp"
#include "ot/user/keyboard/backend.hpp"
#include "ot/user/prog.h"
#include "ot/user/prog/shell/commands.hpp"
#include "ot/user/prog/shell/shell.hpp"
#include "ot/user/tcl.hpp"
#include "ot/user/user.hpp"

// Constants
static const int MAX_OUTPUT_LINES = 100;
static const int MAX_LINE_LENGTH = 256;
static const int TITLE_SIZE = 28;
static const int SUBTITLE_SIZE = 14;
static const int BODY_SIZE = 16;
static const int TEXT_START_X = 15;
static const int TEXT_START_Y = 80;
static const int LINE_SPACING = 20;

using ou::string_view;
using tcl::Interp;
using tcl::Status;

// UI Shell storage
struct UIShellStorage : public shell::ShellStorage {
  char input_buffer[MAX_LINE_LENGTH];
  size_t input_pos;
  GraphicsClient gfxc;
  KeyboardClient kbdc;
  app::Framework *app;

  // Circular buffer for output lines (dynamically allocated)
  char **output_lines; // Array of pointers to lines
  int output_start;    // Index of first line in buffer
  int output_count;    // Number of lines in buffer
  int scroll_offset;   // Lines scrolled from bottom (0 = at bottom)

  bool cursor_visible;
  int cursor_blink_counter;

  UIShellStorage() {
    process_storage_init(50); // Need more pages for TTF rendering

    // Allocate output buffer dynamically
    output_lines = (char **)ou_malloc(MAX_OUTPUT_LINES * sizeof(char *));
    for (int i = 0; i < MAX_OUTPUT_LINES; i++) {
      output_lines[i] = (char *)ou_malloc(MAX_LINE_LENGTH);
      output_lines[i][0] = '\0';
    }

    input_pos = 0;
    output_start = 0;
    output_count = 0;
    scroll_offset = 0;
    cursor_visible = true;
    cursor_blink_counter = 0;
    memset(input_buffer, 0, MAX_LINE_LENGTH);
  }

  // Add a line to output buffer
  void add_output_line(const char *text) {
    int write_idx = (output_start + output_count) % MAX_OUTPUT_LINES;

    if (output_count == MAX_OUTPUT_LINES) {
      // Buffer is full, overwrite oldest line
      output_start = (output_start + 1) % MAX_OUTPUT_LINES;
    } else {
      output_count++;
    }

    snprintf(output_lines[write_idx], MAX_LINE_LENGTH, "%s", text);

    // Auto-scroll to bottom on new output
    scroll_offset = 0;
  }

  // Get line at index (0 = oldest)
  const char *get_output_line(int idx) {
    if (idx >= output_count)
      return nullptr;
    int real_idx = (output_start + idx) % MAX_OUTPUT_LINES;
    return output_lines[real_idx];
  }

  // Clear all output
  void clear_output() {
    output_start = 0;
    output_count = 0;
  }
};

void handle_key_event(UIShellStorage *s, tcl::Interp &i, uint16_t code, uint8_t flags) {
  // Only process key press events
  if (!(flags & KEY_FLAG_PRESSED)) {
    return;
  }

  bool ctrl = (flags & KEY_FLAG_CTRL) != 0;

  // Ctrl+U - Page up (scroll back in history)
  if (ctrl && code == KEY_U) {
    int available_height = 700 - TEXT_START_Y - 40; // Approximate
    int page_size = available_height / LINE_SPACING;
    s->scroll_offset += page_size;
    // Clamp to available scrollback
    int max_scroll = s->output_count > page_size ? s->output_count - page_size : 0;
    if (s->scroll_offset > max_scroll) {
      s->scroll_offset = max_scroll;
    }
    return;
  }

  // Ctrl+D - Page down (scroll forward)
  if (ctrl && code == KEY_D) {
    int available_height = 700 - TEXT_START_Y - 40;
    int page_size = available_height / LINE_SPACING;
    s->scroll_offset -= page_size;
    if (s->scroll_offset < 0) {
      s->scroll_offset = 0;
    }
    return;
  }

  // Backspace
  if (code == KEY_BACKSPACE) {
    if (s->input_pos > 0) {
      s->input_pos--;
      s->input_buffer[s->input_pos] = 0;
    }
    s->cursor_blink_counter = 0;
    s->cursor_visible = true;
    return;
  }

  // Enter - execute command
  if (code == KEY_ENTER) {
    // Add input line to output with prompt
    char prompt_line[MAX_LINE_LENGTH];
    snprintf(prompt_line, MAX_LINE_LENGTH, "> %s", s->input_buffer);
    s->add_output_line(prompt_line);

    // Evaluate TCL command
    s->input_buffer[s->input_pos] = 0;
    tcl::Status status = i.eval(s->input_buffer);

    // Add result to output
    if (status != tcl::S_OK) {
      char error_line[MAX_LINE_LENGTH];
      snprintf(error_line, MAX_LINE_LENGTH, "error: %s", i.result.c_str());
      s->add_output_line(error_line);
    } else {
      // Always show result (even if empty) to provide feedback
      if (i.result.size() > 0) {
        // Split multi-line results
        const char *result = i.result.c_str();
        char line_buf[MAX_LINE_LENGTH];
        int line_pos = 0;

        for (size_t j = 0; j <= i.result.size(); j++) {
          if (j == i.result.size() || result[j] == '\n') {
            line_buf[line_pos] = 0;
            if (line_pos > 0) { // Only add non-empty lines
              s->add_output_line(line_buf);
            }
            line_pos = 0;
          } else if (line_pos < MAX_LINE_LENGTH - 1) {
            line_buf[line_pos++] = result[j];
          }
        }
      }
      // Note: we don't add anything for empty results to keep UI clean
      // Commands like "puts" that want output should use add_output_line directly
    }

    // Clear input
    s->input_pos = 0;
    s->input_buffer[0] = 0;
    s->cursor_blink_counter = 0;
    s->cursor_visible = true;
    return;
  }

  // Regular character input
  bool shift = (flags & KEY_FLAG_SHIFT) != 0;
  char ch = keyboard::scancode_to_ascii(code, shift);
  if (ch && s->input_pos < MAX_LINE_LENGTH - 1) {
    s->input_buffer[s->input_pos++] = ch;
    s->input_buffer[s->input_pos] = 0;
    s->cursor_blink_counter = 0;
    s->cursor_visible = true;
  }
}

// Basic graphics commands

Status cmd_gfx_rectangle(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("gfx/rectangle", argv, 6, 6)) {
    return tcl::S_ERR;
  }

  UIShellStorage *s = (UIShellStorage *)local_storage;

  BoolResult<int> color_result = parse_int(argv[1].c_str());
  if (color_result.is_err()) {
    i.result = "Invalid color";
    return tcl::S_ERR;
  }
  int color = color_result.value();

  BoolResult<int> x_result = parse_int(argv[2].c_str());
  if (x_result.is_err()) {
    i.result = "Invalid x";
    return tcl::S_ERR;
  }
  int x = x_result.value();

  BoolResult<int> y_result = parse_int(argv[3].c_str());
  if (y_result.is_err()) {
    i.result = "Invalid y";
    return tcl::S_ERR;
  }
  int y = y_result.value();

  BoolResult<int> width_result = parse_int(argv[4].c_str());
  if (width_result.is_err()) {
    i.result = "Invalid width";
    return tcl::S_ERR;
  }
  int width = width_result.value();

  BoolResult<int> height_result = parse_int(argv[5].c_str());
  if (height_result.is_err()) {
    i.result = "Invalid height";
    return tcl::S_ERR;
  }
  int height = height_result.value();

  oprintf("gfx/rectangle: x=%d y=%d width=%d height=%d color=%d\n", x, y, width, height, color);

  s->app->fill_rect(x, y, width, height, color);

  return tcl::S_OK;
}

tcl::Status cmd_gfx_loop(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("gfx/loop", argv, 3, 3)) {
    return tcl::S_ERR;
  }

  UIShellStorage *s = (UIShellStorage *)local_storage;

  auto fb_result = s->gfxc.get_framebuffer();
  if (fb_result.is_err()) {
    oprintf("gfx/loop: failed to get framebuffer: %d\n", fb_result.error());
    return tcl::S_ERR;
  }
  auto fb_info = fb_result.value();
  uint32_t *fb = (uint32_t *)fb_info.fb_ptr;
  int width = (int)fb_info.width;
  int height = (int)fb_info.height;

  app::Framework gfx(fb, width, height);

  // Framerate
  BoolResult<int> framerate_result = parse_int(argv[1].c_str());
  if (framerate_result.is_err()) {
    i.result = "Invalid framerate";
    return tcl::S_ERR;
  }

  graphics::FrameManager fm(framerate_result.value());
  oprintf("gfx/loop: starting loop at %d FPS\n", framerate_result.value());
  while (s->running) {
    auto should = s->gfxc.should_render();
    if (should.is_err()) {
      oprintf("gfx/loop: should_render error: %d\n", should.error());
      return tcl::S_ERR;
    }
    if (should.value() == 0) {
      ou_yield();
      continue;
    }

    if (fm.begin_frame()) {
      gfx.clear(0xff0000ff);

      Status s = i.eval(string_view(argv[2]));
      if (s != tcl::S_OK)
        break;

      ou_yield();

      fm.end_frame();
    }
  }

  return tcl::S_OK;
}

Status cmd_gfx_loop_iter(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("gfx/loop-iter", argv, 1, 2)) {
    return tcl::S_ERR;
  }

  // oprintf("gfx/loop iter-called\n");
  UIShellStorage *s = (UIShellStorage *)local_storage;

  // oprintf("kbdc pid %d\n", s->kbdc.pid_);
  auto key_result = s->kbdc.poll_key();
  if (key_result.is_err()) {
    oprintf("gfx/loop-iter: poll_key error: %d\n", key_result.error());
    return tcl::S_ERR;
  }
  auto key_data = key_result.value();

  bool consumed = s->app->pass_key_to_server(s->gfxc, key_data.code, key_data.flags);

  if (key_data.code != 0) {
    oprintf("non-zero key code %d\n", key_data.code);
  }

  // Alt-Q to quit
  if (key_data.flags & KEY_FLAG_ALT) {
    if (key_data.code == KEY_Q) {
      oprintf("gfx loop iter: quitting\n");
      return tcl::S_BREAK;
    }
  }

  s->gfxc.flush();

  return tcl::S_OK;
}

void uishell_main() {
  void *storage_page = ou_get_storage().as_ptr();
  UIShellStorage *s = new (storage_page) UIShellStorage();

  oprintf("UISHELL: Starting graphical shell\n");

  // Yield to let drivers initialize
  ou_yield();

  // Look up graphics driver
  Pid gfx_pid = ou_proc_lookup("graphics");
  if (gfx_pid == PID_NONE) {
    oprintf("UISHELL: Failed to find graphics driver\n");
    ou_exit();
  }

  // Look up keyboard driver
  Pid kbd_pid = ou_proc_lookup("keyboard");
  if (kbd_pid == PID_NONE) {
    oprintf("UISHELL: Failed to find keyboard driver\n");
    ou_exit();
  }

  s->gfxc.set_pid(gfx_pid);
  s->kbdc.set_pid(kbd_pid);

  // Register with graphics server as an app
  auto reg_result = s->gfxc.register_app("uishell");
  if (reg_result.is_err()) {
    oprintf("UISHELL: Failed to register with graphics driver: %d\n", reg_result.error());
    ou_exit();
  }
  oprintf("UISHELL: Registered as app %lu\n", reg_result.value());

  // Get framebuffer info
  auto fb_result = s->gfxc.get_framebuffer();
  if (fb_result.is_err()) {
    oprintf("UISHELL: Failed to get framebuffer: %d\n", fb_result.error());
    ou_exit();
  }

  auto fb_info = fb_result.value();
  uint32_t *fb = (uint32_t *)fb_info.fb_ptr;
  int width = (int)fb_info.width;
  int height = (int)fb_info.height;

  oprintf("UISHELL: Framebuffer %dx%d\n", width, height);

  // Create graphics framework
  app::Framework *gfxh = new (ou_malloc(sizeof(app::Framework))) app::Framework(fb, width, height);
  app::Framework &gfx = *gfxh;

  s->app = gfxh;

  // Initialize TTF font
  auto ttf_result = gfx.init_ttf();
  if (ttf_result.is_err()) {
    oprintf("UISHELL: Failed to init TTF font: %s\n", error_code_to_string(ttf_result.error()));
    ou_exit();
  }
  oprintf("UISHELL: TTF font initialized\n");

  // Allocate page for messagepack
  char *mp_page = (char *)ou_alloc_page();

  // Initialize TCL interpreter
  tcl::Interp i;

  tcl::register_core_commands(i);
  i.register_mpack_functions(mp_page, OT_PAGE_SIZE);
  register_ipc_method_vars(i);

  i.set_var("features_ui", "1");
  i.set_var("uishell_output_to_console", "0");

  // Register shared shell commands
  shell::register_shell_commands(i);

  // Register UI shell-specific commands
  i.register_command(
      "clear",
      [](tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) -> tcl::Status {
        ((UIShellStorage *)local_storage)->clear_output();
        return tcl::S_OK;
      },
      nullptr, "[clear] - Clear output history");

  // Override puts to output to screen instead of console
  i.register_command(
      "puts",
      [](tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) -> tcl::Status {
        UIShellStorage *s = (UIShellStorage *)local_storage;
        if (!i.arity_check("puts", argv, 2, 2)) {
          return tcl::S_ERR;
        }
        auto output_to_console_var = i.get_var("uishell_output_to_console");
        auto output_to_console = parse_int(output_to_console_var->val->c_str());
        if (output_to_console.is_ok() && output_to_console.value() == 1) {
          oprintf("puts: %s\n", argv[1].c_str());
        } else {
          s->add_output_line(argv[1].c_str());
        }
        return tcl::S_OK;
      },
      nullptr, "[puts string] - Print string to screen");

  i.register_command("gfx/loop", cmd_gfx_loop, nullptr,
                     "[gfx/loop framerate:int body:string] - Loop a body at a given framerate");

  i.register_command("gfx/rect", cmd_gfx_rectangle, nullptr,
                     "[gfx/rect color:int x:int y:int width:int height:int] - Draw a rectangle");

  i.register_command("gfx/loop-iter", cmd_gfx_loop_iter, nullptr,
                     "[gfx/loop-iter] - Should be called in gfx/loop body to properly yield to operating system");

  // Execute shellrc startup script
#include "shellrc.hpp"
  tcl::Status shellrc_status = i.eval(tcl::string_view(shellrc_content));
  if (shellrc_status != tcl::S_OK) {
    s->add_output_line("shellrc error");
    s->add_output_line(i.result.c_str());
  }

  s->add_output_line("OTIUM Graphical Shell");
  s->add_output_line("Type 'help' for commands");

  // Run at 60 FPS
  graphics::FrameManager fm(60);

  oprintf("UISHELL: Running\n");

  while (s->running) {
    // Check if we should render (are we the active app?)
    auto should = s->gfxc.should_render();
    if (should.is_err()) {
      oprintf("UISHELL: should_render returned error: %d\n", should.error());
      ou_exit();
    }
    if (should.value() == 0) {
      // Not active, just yield
      ou_yield();
      continue;
    }

    if (fm.begin_frame()) {
      // Poll keyboard
      auto key_result = s->kbdc.poll_key();
      if (key_result.is_err()) {
        oprintf("UISHELL: poll_key error: %d\n", key_result.error());
      } else {
        auto key_data = key_result.value();
        if (key_data.has_key) {
          // oprintf("UISHELL: got key code=%d flags=%d\n", (int)key_data.code, (int)key_data.flags);
          // Pass key to graphics server first for global hotkeys (Alt+1-9 app switching)
          bool consumed = gfx.pass_key_to_server(s->gfxc, key_data.code, key_data.flags);
          // oprintf("UISHELL: pass_key_to_server returned %d\n", consumed);
          if (!consumed) {
            // Key not consumed by server, handle locally
            handle_key_event(s, i, key_data.code, key_data.flags);
          }
        }
      }

      // Update cursor blink
      s->cursor_blink_counter++;
      if (s->cursor_blink_counter >= 30) { // Blink every 0.5s at 60 FPS
        s->cursor_visible = !s->cursor_visible;
        s->cursor_blink_counter = 0;
      }

      // Clear screen to dark blue
      gfx.clear(0xFF1A1A2E);

      // Draw title
      gfx.draw_ttf_text(TEXT_START_X, 15, "OTIUM SHELL", 0xFFEEEEEE, TITLE_SIZE);
      gfx.draw_ttf_text(TEXT_START_X, 48, "Interactive TCL Shell", 0xFFCCCCCC, SUBTITLE_SIZE);

      // Draw separator line
      gfx.draw_hline(TEXT_START_X, 68, width - TEXT_START_X * 2, 0xFF444444);

      // Calculate how many lines fit on screen
      int available_height = height - TEXT_START_Y - 40; // Leave margin at bottom
      int max_visible_lines = available_height / LINE_SPACING;

      // Draw output history (most recent lines at bottom, adjusted for scroll)
      int y = TEXT_START_Y;
      int start_line =
          (s->output_count > max_visible_lines) ? (s->output_count - max_visible_lines - s->scroll_offset) : 0;
      if (start_line < 0)
        start_line = 0;

      // Debug: log output rendering state (once)
      for (int i = start_line; i < s->output_count; i++) {
        const char *line = s->get_output_line(i);
        if (line) {
          gfx.draw_ttf_text(TEXT_START_X, y, line, 0xFFFFFFFF, BODY_SIZE);
          y += LINE_SPACING;
        }
      }

      // Draw prompt and input line
      char prompt_with_input[MAX_LINE_LENGTH + 10];
      snprintf(prompt_with_input, sizeof(prompt_with_input), "> %s", s->input_buffer);
      gfx.draw_ttf_text(TEXT_START_X, y, prompt_with_input, 0xFF88FF88, BODY_SIZE);

      // Draw cursor
      if (s->cursor_visible) {
        // Measure text up to cursor position
        auto measure_result = gfx.measure_ttf_text(prompt_with_input, BODY_SIZE);
        int cursor_x = TEXT_START_X;
        if (measure_result.is_ok()) {
          cursor_x += measure_result.value();
        }
        gfx.draw_ttf_text(cursor_x, y, "_", 0xFFFFFF00, BODY_SIZE);
      }

      // Flush to display
      s->gfxc.flush();
      fm.end_frame();
    }

    // Always yield
    ou_yield();
  }

  // Unregister before exit
  s->gfxc.unregister_app();

  oprintf("UISHELL: Exiting\n");
  ou_exit();
}
