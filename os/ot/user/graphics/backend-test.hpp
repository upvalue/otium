#ifndef OT_USER_GRAPHICS_BACKEND_TEST_HPP
#define OT_USER_GRAPHICS_BACKEND_TEST_HPP

#include "ot/user/graphics/backend.hpp"
#include "ot/user/user.hpp"

/**
 * Test graphics backend for automated testing.
 * Uses a small framebuffer and prints hex dump on flush.
 */
class TestGraphicsBackend : public GraphicsBackend {
private:
  uint32_t *framebuffer;
  uint32_t width;
  uint32_t height;

public:
  TestGraphicsBackend() : framebuffer(nullptr), width(16), height(16) {}

  bool init() override {
    oprintf("TestGraphicsBackend: Initializing %ux%u framebuffer\n", width, height);

    // Allocate framebuffer from user-space page allocator
    uint32_t fb_size = width * height * sizeof(uint32_t);
    uint32_t pages_needed = (fb_size + OT_PAGE_SIZE - 1) / OT_PAGE_SIZE;

    PageAddr fb_page = PageAddr((uintptr_t)ou_alloc_page());
    if (fb_page.raw() == 0) {
      oprintf("TestGraphicsBackend: Failed to allocate framebuffer\n");
      return false;
    }

    framebuffer = fb_page.as<uint32_t>();

    // Clear framebuffer
    for (uint32_t i = 0; i < width * height; i++) {
      framebuffer[i] = 0;
    }

    oprintf("TestGraphicsBackend: Initialized at 0x%lx\n", (uintptr_t)framebuffer);
    return true;
  }

  uint32_t *get_framebuffer() override { return framebuffer; }

  void flush() override {
    if (!framebuffer) {
      oprintf("TestGraphicsBackend: Cannot flush - not initialized\n");
      return;
    }

    oprintf("TEST: Framebuffer %ux%u:\n", width, height);
    for (uint32_t y = 0; y < height; y++) {
      oprintf("TEST: FB[%2u]: ", y);
      for (uint32_t x = 0; x < width; x++) {
        oprintf("%08x ", framebuffer[y * width + x]);
      }
      oprintf("\n");
    }
  }

  uint32_t get_width() const override { return width; }

  uint32_t get_height() const override { return height; }
};

#endif
