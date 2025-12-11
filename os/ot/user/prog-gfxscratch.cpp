// prog-gfxscratch.cpp - Minimal graphical program for debugging
// Toggle to test with/without app::Framework
#define USE_APP_FRAMEWORK 1
// Toggle to exit after 10 frames (vs run forever)
#define EXIT_AFTER_10_FRAMES 1
// If USE_APP_FRAMEWORK=1, toggle whether to init TTF font
#define INIT_TTF_FONT 1
// If USE_APP_FRAMEWORK=1, toggle whether to actually draw with TTF
#define DRAW_WITH_TTF 1

#include "ot/user/gen/graphics-client.hpp"
#include "ot/user/local-storage.hpp"
#include "ot/user/prog.h"
#include "ot/user/user.hpp"

#if USE_APP_FRAMEWORK
#include "ot/lib/app-framework.hpp"
#endif

// Storage - needs more pages if using app::Framework
struct GfxScratchStorage : public LocalStorage {
  bool running;
  int frame_count;

  GfxScratchStorage() {
#if USE_APP_FRAMEWORK
    process_storage_init(25); // More pages for TTF rendering
#else
    process_storage_init(1); // Minimal: 1 page
#endif
    running = true;
    frame_count = 0;
  }
};

void gfxscratch_main() {
  oprintf("GFXSCRATCH: Starting minimal graphics test\n");

  void *storage_page = ou_get_storage().as_ptr();
  GfxScratchStorage *s = new (storage_page) GfxScratchStorage();

  ou_yield();

  // Look up graphics driver
  Pid gfx_pid = ou_proc_lookup("graphics");
  if (gfx_pid == PID_NONE) {
    oprintf("GFXSCRATCH: Failed to find graphics driver\n");
    ou_exit();
  }
  oprintf("GFXSCRATCH: Found graphics at PID %lu\n", gfx_pid.raw());

  GraphicsClient gfx_client(gfx_pid);
  oprintf("GFXSCRATCH: gfx_client at %p with pid %lu\n", &gfx_client, gfx_client.pid_.raw());

  // Register with graphics server
  auto reg_result = gfx_client.register_app("gfxscratch");
  if (reg_result.is_err()) {
    oprintf("GFXSCRATCH: Failed to register: %d\n", reg_result.error());
    ou_exit();
  }
  oprintf("GFXSCRATCH: Registered as app %lu\n", reg_result.value());

  // Get framebuffer
  auto fb_result = gfx_client.get_framebuffer();
  if (fb_result.is_err()) {
    oprintf("GFXSCRATCH: Failed to get framebuffer: %d\n", fb_result.error());
    ou_exit();
  }

  auto fb_info = fb_result.value();
  uint32_t *fb = (uint32_t *)fb_info.fb_ptr;
  int width = (int)fb_info.width;
  int height = (int)fb_info.height;
  oprintf("GFXSCRATCH: Framebuffer %dx%d at %p\n", width, height, fb);

#if USE_APP_FRAMEWORK
  oprintf("GFXSCRATCH: Creating app::Framework\n");
  app::Framework gfx(fb, width, height);
  oprintf("GFXSCRATCH: app::Framework created\n");

#if INIT_TTF_FONT
  oprintf("GFXSCRATCH: Initializing TTF font\n");
  auto ttf_result = gfx.init_ttf();
  if (ttf_result.is_err()) {
    oprintf("GFXSCRATCH: TTF init failed: %d\n", ttf_result.error());
    ou_exit();
  }
  oprintf("GFXSCRATCH: TTF font initialized\n");
#endif
#endif

  oprintf("GFXSCRATCH: Running main loop\n");

  while (s->running) {
    // oprintf("GFXSCRATCH: gfx_client at %p with pid %lu (frame %d)\n", &gfx_client, gfx_client.pid_.raw(),
    //  s->frame_count);

    auto should = gfx_client.should_render();
    if (should.is_err()) {
      // oprintf("GFXSCRATCH: should_render error: %d\n", should.error());
      ou_exit();
    }

    if (should.value()) {
#if USE_APP_FRAMEWORK
      // Use Framework to clear
      gfx.clear(0xFF002200 | ((s->frame_count * 4) & 0xFF));
#if DRAW_WITH_TTF
      gfx.draw_ttf_text(50, 50, "GFXSCRATCH with Framework", 0xFFFFFFFF, 24);
#endif
#else
      // Simple: just fill with a color based on frame count
      uint32_t color = 0xFF000000 | ((s->frame_count * 4) & 0xFF);
      for (int i = 0; i < width * height; i++) {
        fb[i] = color;
      }
#endif

      gfx_client.flush();
      s->frame_count++;

#if EXIT_AFTER_10_FRAMES
      if (s->frame_count >= 10) {
        oprintf("GFXSCRATCH: Exiting after 10 frames\n");
        s->running = false;
      }
#endif
    }

    ou_yield();
  }

  // Unregister before exit
  gfx_client.unregister_app();

  oprintf("GFXSCRATCH: Exiting\n");
  ou_exit();
}
