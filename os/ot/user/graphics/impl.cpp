#include "ot/user/gen/graphics-server.hpp"
#include "ot/user/graphics/backend.hpp"

#if OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_TEST
#include "ot/user/graphics/backend-test.hpp"
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_VIRTIO
#include "ot/user/graphics/backend-virtio.hpp"
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_SDL
#include "ot/user/graphics/backend-sdl.hpp"
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_WASM
#include "ot/user/graphics/backend-wasm.hpp"
#endif

// Placement new operator (required for freestanding environment)
inline void *operator new(decltype(sizeof(0)), void *ptr) noexcept { return ptr; }

// Global backend instance (initialized in proc_graphics)
static GraphicsBackend *g_backend = nullptr;

Result<GetFramebufferResult, ErrorCode> GraphicsServer::handle_get_framebuffer() {
  if (!g_backend || !g_backend->get_framebuffer()) {
    return Result<GetFramebufferResult, ErrorCode>::err(GRAPHICS__NOT_INITIALIZED);
  }

  GetFramebufferResult result;
  result.fb_ptr = (uintptr_t)g_backend->get_framebuffer();
  result.width = g_backend->get_width();
  result.height = g_backend->get_height();

  oprintf("[graphics] Returning fb_ptr=0x%lx, width=%lu, height=%lu\n",
          result.fb_ptr, result.width, result.height);

  return Result<GetFramebufferResult, ErrorCode>::ok(result);
}

Result<bool, ErrorCode> GraphicsServer::handle_flush() {
  if (!g_backend) {
    return Result<bool, ErrorCode>::err(GRAPHICS__NOT_INITIALIZED);
  }

  g_backend->flush();
  return Result<bool, ErrorCode>::ok(true);
}

void proc_graphics(void) {
  oprintf("Graphics driver starting...\n");

// Select and initialize backend based on compile-time configuration
#if OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_TEST
  oprintf("Using test graphics backend\n");
  // Use placement new to avoid static initialization guards
  static char backend_buffer[sizeof(TestGraphicsBackend)] __attribute__((aligned(alignof(TestGraphicsBackend))));
  TestGraphicsBackend *backend_ptr = new (backend_buffer) TestGraphicsBackend();
  TestGraphicsBackend &backend = *backend_ptr;
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_VIRTIO
  oprintf("Using VirtIO graphics backend\n");
  // Find VirtIO GPU device
  static char backend_buffer[sizeof(VirtioGraphicsBackend)] __attribute__((aligned(alignof(VirtioGraphicsBackend))));
  VirtioGraphicsBackend *backend_ptr = nullptr;

  for (int i = 0; i < VIRTIO_MMIO_COUNT; i++) {
    uintptr_t addr = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE);
    VirtIODevice test_dev(addr);
    test_dev.device_id = test_dev.read_reg(VIRTIO_MMIO_DEVICE_ID);

    if (test_dev.is_valid() && test_dev.device_id == VIRTIO_ID_GPU) {
      // Construct VirtioGraphicsBackend in-place using placement new
      backend_ptr = new (backend_buffer) VirtioGraphicsBackend(addr);
      break;
    }
  }

  if (!backend_ptr) {
    oprintf("ERROR: No VirtIO GPU device found!\n");
    ou_exit();
  }

  VirtioGraphicsBackend &backend = *backend_ptr;
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_SDL
  oprintf("Using SDL graphics backend\n");
  // Use placement new to avoid static initialization guards
  static char backend_buffer[sizeof(SdlGraphicsBackend)] __attribute__((aligned(alignof(SdlGraphicsBackend))));
  SdlGraphicsBackend *backend_ptr = new (backend_buffer) SdlGraphicsBackend();
  SdlGraphicsBackend &backend = *backend_ptr;
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_WASM
  oprintf("Using WASM graphics backend\n");
  // Use placement new to avoid static initialization guards
  static char backend_buffer[sizeof(WasmGraphicsBackend)] __attribute__((aligned(alignof(WasmGraphicsBackend))));
  WasmGraphicsBackend *backend_ptr = new (backend_buffer) WasmGraphicsBackend();
  WasmGraphicsBackend &backend = *backend_ptr;
#else
#error "Unknown graphics backend"
#endif

  g_backend = &backend;

  if (!backend.init()) {
    oprintf("ERROR: Failed to initialize graphics backend\n");
    ou_exit();
  }

  oprintf("Graphics driver initialized successfully\n");
  oprintf("Framebuffer: %ux%u at 0x%lx\n", backend.get_width(), backend.get_height(),
          (uintptr_t)backend.get_framebuffer());

  // Run IPC server loop
  GraphicsServer server;
  server.run();
}
