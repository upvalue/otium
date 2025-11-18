// prog-spacedemo.cpp - DOS Space Demo port
#include "ot/lib/frame-manager.hpp"
#include "ot/lib/gfx-util.hpp"
#include "ot/lib/messages.hpp"
#include "ot/user/gen/graphics-client.hpp"
#include "ot/user/local-storage.hpp"
#include "ot/user/prog.h"
#include "ot/user/string.hpp"
#include "ot/user/user.hpp"

// Color constants (BGRA format: 0xAARRGGBB)
static const uint32_t COLOR_BLACK = 0xFF000000;
static const uint32_t COLOR_WHITE = 0xFFFFFFFF;
static const uint32_t COLOR_YELLOW = 0xFF00FFFF;
static const uint32_t COLOR_CYAN = 0xFFFFFF00;
static const uint32_t COLOR_MAGENTA = 0xFFFF00FF;

// SpaceDemo-specific storage
struct SpaceDemoStorage : public LocalStorage {
  SpaceDemoStorage() {
    // Initialize memory allocator with 1 page for now (just testing)
    process_storage_init(1);
  }
};

void spacedemo_main() {
  oprintf("SPACEDEMO: Starting\n");

  // Get the storage page and initialize SpaceDemoStorage
  void *storage_page = ou_get_storage().as_ptr();
  SpaceDemoStorage *s = new (storage_page) SpaceDemoStorage();

  // Yield to let graphics driver initialize
  ou_yield();

  // Look up graphics driver
  int gfx_pid = ou_proc_lookup("graphics");
  if (gfx_pid == 0) {
    oprintf("SPACEDEMO: Failed to find graphics driver\n");
    ou_exit();
  }
  oprintf("SPACEDEMO: Found graphics driver at PID %d\n", gfx_pid);

  GraphicsClient client(gfx_pid);

  // Get framebuffer info
  auto fb_result = client.get_framebuffer();
  if (fb_result.is_err()) {
    oprintf("SPACEDEMO: Failed to get framebuffer: %d\n", fb_result.error());
    ou_exit();
  }

  auto fb_info = fb_result.value();
  uint32_t *fb = (uint32_t *)fb_info.fb_ptr;
  int width = (int)fb_info.width;
  int height = (int)fb_info.height;

  oprintf("SPACEDEMO: Got framebuffer at 0x%lx, %dx%d\n", fb_info.fb_ptr,
          width, height);

  // Create graphics utility wrapper
  gfx::GfxUtil gfx(fb, width, height);

  // Run demo at 30 FPS
  graphics::FrameManager fm(30);

  int frame_count = 0;
  const int max_frames = 120; // Run for 4 seconds (120 frames at 30 FPS)

  while (frame_count < max_frames) {
    if (fm.begin_frame()) {
      // Clear screen to black
      gfx.clear(COLOR_BLACK);

      // Draw some test text
      gfx.draw_text(10, 10, "DOS SPACE DEMO", COLOR_CYAN, 2);
      gfx.draw_text(10, 30, "Frame rate: 30 FPS", COLOR_WHITE, 1);

      // Draw a centered message
      int center_x = width / 2 - 50;
      int center_y = height / 2 - 10;
      gfx.draw_text(center_x, center_y, "Graphics Test", COLOR_YELLOW, 2);

      // Draw some shapes to test the drawing functions
      gfx.draw_gradient_circle(width / 4, height / 2, 40, COLOR_YELLOW,
                               COLOR_MAGENTA);
      gfx.draw_gradient_circle(3 * width / 4, height / 2, 30, COLOR_CYAN,
                               COLOR_BLACK);

      // Draw frame counter
      char frame_text[32];
      osnprintf(frame_text, sizeof(frame_text), "Frame: %d/%d", frame_count,
                max_frames);
      gfx.draw_text(10, height - 20, frame_text, COLOR_WHITE, 1);

      // Flush to display
      auto flush_result = client.flush();
      if (flush_result.is_err()) {
        oprintf("SPACEDEMO: Flush failed: %d\n", flush_result.error());
        break;
      }

      fm.end_frame();
      frame_count++;
    }

    // Always yield to cooperate with other processes
    ou_yield();
  }

  oprintf("SPACEDEMO: Complete (%d frames)\n", frame_count);
  ou_exit();
}
