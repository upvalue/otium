// prog-scratch.cpp - Scratch program for testing and experimentation
#include "ot/lib/frame-manager.hpp"
#include "ot/lib/messages.hpp"
#include "ot/user/gen/graphics-client.hpp"
#include "ot/user/local-storage.hpp"
#include "ot/user/string.hpp"
#include "ot/user/user.hpp"

#include "ot/user/prog.h"

// Scratch-specific storage inheriting from LocalStorage
struct ScratchStorage : public LocalStorage {
  ScratchStorage() {
    // Initialize memory allocator with 1 page
    process_storage_init(1);
  }
};

// Simple PRNG for visual randomness
static uint32_t rng_state = 0x12345678;

uint32_t simple_rand() {
  // Xorshift32
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

void scratch_main() {
  oprintf("SCRATCH: Purple static demo starting\n");

  // Get the storage page and initialize ScratchStorage
  void *storage_page = ou_get_storage().as_ptr();
  ScratchStorage *s = new (storage_page) ScratchStorage();

  // Yield to let graphics driver initialize
  ou_yield();

  // Look up graphics driver
  Pid gfx_pid = ou_proc_lookup("graphics");
  if (gfx_pid == PID_NONE) {
    oprintf("SCRATCH: Failed to find graphics driver\n");
    ou_exit();
  }
  oprintf("SCRATCH: Found graphics driver at PID %lu\n", gfx_pid.raw());

  Pid kbd_pid = ou_proc_lookup("keyboard");
  if (kbd_pid == PID_NONE) {
    oprintf("SCRATCH: Failed to find keyboard driver\n");
    ou_exit();
  }
  oprintf("SCRATCH: Found keyboard driver at PID %lu\n", kbd_pid.raw());

  GraphicsClient client(gfx_pid);
  // KeyboardClient kbd(kbd_pid);

  // Register with graphics driver
  auto reg_result = client.register_app("scratch");
  if (reg_result.is_err()) {
    oprintf("SCRATCH: Failed to register with graphics driver: %d\n", reg_result.error());
    ou_exit();
  }
  oprintf("SCRATCH: Registered as app %lu\n", reg_result.value());

  // Get framebuffer info
  auto fb_result = client.get_framebuffer();
  if (fb_result.is_err()) {
    oprintf("SCRATCH: Failed to get framebuffer: %d\n", fb_result.error());
    ou_exit();
  }

  auto fb_info = fb_result.value();
  uint32_t *fb = (uint32_t *)fb_info.fb_ptr;
  uint32_t width = (uint32_t)fb_info.width;
  uint32_t height = (uint32_t)fb_info.height;

  oprintf("SCRATCH: Got framebuffer at 0x%lx, %lux%lu\n", fb_info.fb_ptr, fb_info.width, fb_info.height);

  // Animate purple static at 30 FPS
  graphics::FrameManager fm(30); // Target 30 FPS

  const int num_frames = 60; // Run for 60 frames
  int frames_rendered = 0;

  bool running = true;

  while (running) {
    // Check if we should render (are we the active app?)
    auto should = client.should_render();
    if (should.is_err() || should.value() == 0) {
      ou_yield();
      continue;
    }

    if (fm.begin_frame()) {

      // Fill screen with random purplish static
      for (uint32_t i = 0; i < width * height; i++) {
        uint32_t rand_val = simple_rand();

        // Create purplish colors by biasing red and blue channels
        uint8_t r = (rand_val & 0xFF);
        uint8_t g = (rand_val >> 8) & 0x7F;    // Less green (darker)
        uint8_t b = ((rand_val >> 16) & 0xFF); // Full blue range

        // BGRA format: 0xAARRGGBB
        fb[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
      }

      // Flush to display
      auto flush_result = client.flush();
      if (flush_result.is_err()) {
        oprintf("SCRATCH: Flush failed: %d\n", flush_result.error());
        break;
      }

      fm.end_frame();
      frames_rendered++;
    }

    // Always yield to cooperate with other processes
    ou_yield();
  }

  // Unregister before exit
  client.unregister_app();

  oprintf("SCRATCH: Purple static demo complete (%d frames)\n", num_frames);
  ou_exit();
}
