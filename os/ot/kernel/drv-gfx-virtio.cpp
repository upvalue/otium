#include "ot/kernel/drv-gfx-virtio.hpp"

// Global GPU instance (to avoid dynamic allocation)
static VirtIOGPU g_gpu;

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
  VirtIOGPU *gpu = nullptr;

  for (int i = 0; i < VIRTIO_MMIO_COUNT; i++) {
    uintptr_t addr = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE);
    VirtIODevice test_dev(addr);
    test_dev.device_id = test_dev.read_reg(VIRTIO_MMIO_DEVICE_ID);

    if (test_dev.is_valid() && test_dev.device_id == VIRTIO_ID_GPU) {
      g_gpu = VirtIOGPU(addr);
      gpu = &g_gpu;
      break;
    }
  }

  if (!gpu) {
    oprintf("No VirtIO GPU found!\n");
    current_proc->state = TERMINATED;
    while (true) {
      yield();
    }
    return;
  }

  // Initialize the GPU
  if (!gpu->init()) {
    oprintf("Failed to initialize GPU\n");
    current_proc->state = TERMINATED;
    while (true) {
      yield();
    }
    return;
  }

  // Create framebuffer
  gpu->create_framebuffer();

  oprintf("Starting animated static effect...\n");

  // Animate static effect
  uint32_t *fb = gpu->framebuffer.as<uint32_t>();
  uint32_t frame = 0;
  while (true) {
    // Fill screen with random grayscale pixels
    for (uint32_t i = 0; i < gpu->width * gpu->height; i++) {
      uint8_t gray = rand_u32() & 0xFF;
      // fb[i] = 0xFF000000 | (gray << 16) | (gray << 8) | gray; // BGRA: gray on all channels
      fb[i] = 0xFF0000FF;
    }

    // Flush to display
    gpu->flush();

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
