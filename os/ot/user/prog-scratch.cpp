// prog-scratch.cpp - Scratch program for testing and experimentation
#include "ot/lib/messages.hpp"
#include "ot/user/gen/graphics-client.hpp"
#include "ot/user/local-storage.hpp"
#include "ot/user/string.hpp"
#include "ot/user/user.hpp"

#include "ot/user/prog-scratch.h"

#define SCRATCH_PAGES 5

// Scratch-specific storage inheriting from LocalStorage
struct ScratchStorage : public LocalStorage {
  ScratchStorage() {
    // Initialize memory allocator
    process_storage_init(SCRATCH_PAGES);
  }
};

void scratch_main() {
  oprintf("SCRATCH: Graphics demo starting\n");

  // Get the storage page and initialize ScratchStorage
  void *storage_page = ou_get_storage().as_ptr();
  ScratchStorage *s = new (storage_page) ScratchStorage();

  // Yield to let graphics driver initialize
  ou_yield();

  // Look up graphics driver
  int gfx_pid = ou_proc_lookup("graphics");
  if (gfx_pid == 0) {
    oprintf("SCRATCH: Failed to find graphics driver\n");
    ou_exit();
  }
  oprintf("SCRATCH: Found graphics driver at PID %d\n", gfx_pid);

  GraphicsClient client(gfx_pid);

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

  // Fill entire screen with blue (BGRA format: 0xAABBGGRR)
  for (uint32_t i = 0; i < width * height; i++) {
    fb[i] = 0xFF0000FF; // Blue in BGRA
  }

  oprintf("SCRATCH: Filled screen with blue\n");

  // Flush to display
  auto flush_result = client.flush();
  if (flush_result.is_ok()) {
    oprintf("SCRATCH: Flushed framebuffer\n");
  } else {
    oprintf("SCRATCH: Flush failed: %d\n", flush_result.error());
  }

  oprintf("SCRATCH: Graphics demo complete\n");
  ou_exit();
}
