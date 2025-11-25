#ifndef OT_USER_GRAPHICS_BACKEND_WASM_HPP
#define OT_USER_GRAPHICS_BACKEND_WASM_HPP

#include "ot/user/graphics/backend.hpp"
#include "ot/user/user.hpp"

#include <emscripten.h>

// EM_JS shims for graphics operations
// These call into JavaScript, which can use Canvas (browser) or SDL (node)
// clang-format off
EM_JS(bool, js_graphics_init, (int width, int height), {
  console.log('[EM_JS] js_graphics_init called with', width, 'x', height);
  if (typeof Module.graphicsInit === 'function') {
    console.log('[EM_JS] Calling Module.graphicsInit');
    const result = Module.graphicsInit(width, height);
    console.log('[EM_JS] Module.graphicsInit returned', result);
    return result;
  }
  console.error('[EM_JS] Module.graphicsInit not defined');
  return false;
});

EM_JS(void, js_graphics_flush, (uint32_t *fb_ptr, int width, int height), {
  if (typeof Module.graphicsFlush === 'function') {
    // Create a view into WASM memory - no copy needed!
    // fb_ptr is already a byte address, shift right by 2 for uint32 index
    const pixelCount = width * height;
    const pixels = HEAP32.subarray(fb_ptr >> 2, (fb_ptr >> 2) + pixelCount);
    Module.graphicsFlush(pixels, width, height);
  }
});

EM_JS(void, js_graphics_cleanup, (), {
  if (typeof Module.graphicsCleanup === 'function') {
    Module.graphicsCleanup();
  }
});
// clang-format on

/**
 * WASM graphics backend using EM_JS shims.
 * Supports both browser (Canvas) and Node.js (SDL/Canvas) via JavaScript callbacks.
 */
class WasmGraphicsBackend : public GraphicsBackend {
private:
  uint32_t *framebuffer;
  uint32_t width;
  uint32_t height;

public:
  WasmGraphicsBackend() : framebuffer(nullptr), width(1024), height(700) {}

  bool init() override {
    oprintf("WasmGraphicsBackend: Initializing %ux%u framebuffer\n", width, height);

    // Allocate framebuffer from known memory (guaranteed contiguous)
    uint32_t fb_size = width * height * sizeof(uint32_t);
    uint32_t pages_needed = (fb_size + OT_PAGE_SIZE - 1) / OT_PAGE_SIZE;

    oprintf("WasmGraphicsBackend: Locking %u pages for framebuffer\n", pages_needed);

    // Lock known framebuffer memory (guaranteed contiguous)
    void *fb_ptr = ou_lock_known_memory(KNOWN_MEMORY_FRAMEBUFFER, pages_needed);
    if (!fb_ptr) {
      oprintf("WasmGraphicsBackend: Failed to lock framebuffer memory\n");
      return false;
    }

    framebuffer = (uint32_t *)fb_ptr;

    // Clear framebuffer to black
    for (uint32_t i = 0; i < width * height; i++) {
      framebuffer[i] = 0xFF000000; // Black with full alpha
    }

    // Initialize JavaScript graphics system
    if (!js_graphics_init(width, height)) {
      oprintf("WasmGraphicsBackend: JavaScript graphics initialization failed\n");
      return false;
    }

    oprintf("WasmGraphicsBackend: Initialized at 0x%lx\n", (uintptr_t)framebuffer);
    return true;
  }

  ~WasmGraphicsBackend() { js_graphics_cleanup(); }

  uint32_t *get_framebuffer() override { return framebuffer; }

  void flush() override {
    if (!framebuffer) {
      oprintf("WasmGraphicsBackend: Cannot flush - not initialized\n");
      return;
    }

    // Pass framebuffer pointer to JavaScript
    // JS will create a view into WASM memory without copying
    js_graphics_flush(framebuffer, width, height);
  }

  uint32_t get_width() const override { return width; }

  uint32_t get_height() const override { return height; }
};

#endif
