#include "ot/kernel/drv-gfx-virtio.hpp"

// Simple PRNG for static effect
static uint32_t rng_state = 0x12345678;
static uint32_t rand_u32() {
  // Xorshift32
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5;
  return rng_state;
}

void graphics_demo_main_proc() {
  oprintf("=== VirtIO GPU Graphics Demo ===\n");

  // Find and initialize GPU
  // Use placement new to avoid issues with global constructors in freestanding environment
  static char gfx_buffer[sizeof(VirtioGfx)] __attribute__((aligned(alignof(VirtioGfx))));
  VirtioGfx *gfx = nullptr;

  for (int i = 0; i < VIRTIO_MMIO_COUNT; i++) {
    uintptr_t addr = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE);
    VirtIODevice test_dev(addr);
    test_dev.device_id = test_dev.read_reg(VIRTIO_MMIO_DEVICE_ID);

    if (test_dev.is_valid() && test_dev.device_id == VIRTIO_ID_GPU) {
      // Construct VirtioGfx in-place using placement new
      gfx = new (gfx_buffer) VirtioGfx(addr);
      break;
    }
  }

  if (!gfx) {
    oprintf("No VirtIO GPU found!\n");
    current_proc->state = TERMINATED;
    while (true) {
      yield();
    }
    return;
  }

  // Initialize the GPU
  if (!gfx->init()) {
    oprintf("Failed to initialize GPU\n");
    current_proc->state = TERMINATED;
    while (true) {
      yield();
    }
    return;
  }

  // Create framebuffer
  gfx->create_framebuffer();

  oprintf("Starting animated static effect...\n");

  // Animate static effect
  uint32_t frame = 0;
  while (true) {
    // Fill screen with solid blue (user's preference)
    for (uint32_t y = 0; y < gfx->get_height(); y++) {
      for (uint32_t x = 0; x < gfx->get_width(); x++) {
        gfx->put(x, y, 0xFF0000FF);
      }
    }

    // Flush to display
    gfx->flush();

    frame++;
    if (frame % 60 == 0) {
      oprintf("Frame %u\n", frame);
    }

    // Small delay to control frame rate
    for (volatile int i = 0; i < 10000; i++)
      ;
    oprintf("yielding\n");
    yield();
  }
}
