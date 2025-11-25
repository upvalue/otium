#include "ot/lib/logger.hpp"
#include "ot/user/gen/graphics-server.hpp"
#include "ot/user/graphics/backend.hpp"

#if OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_NONE
#include "ot/user/graphics/backend-none.hpp"
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_TEST
#include "ot/user/graphics/backend-test.hpp"
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_VIRTIO
#include "ot/user/graphics/backend-virtio.hpp"
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_WASM
#include "ot/user/graphics/backend-wasm.hpp"
#endif

// Graphics server implementation with instance state
struct GraphicsServer : GraphicsServerBase {
  GraphicsBackend *backend;
  Logger l;

  GraphicsServer() : backend(nullptr), l("gfx") {}

  Result<GetFramebufferResult, ErrorCode> handle_get_framebuffer() override {
    if (!backend || !backend->get_framebuffer()) {
      return Result<GetFramebufferResult, ErrorCode>::err(GRAPHICS__NOT_INITIALIZED);
    }

    GetFramebufferResult result;
    result.fb_ptr = (uintptr_t)backend->get_framebuffer();
    result.width = backend->get_width();
    result.height = backend->get_height();

    l.log("Returning fb_ptr=0x%lx, width=%lu, height=%lu", result.fb_ptr, result.width, result.height);

    return Result<GetFramebufferResult, ErrorCode>::ok(result);
  }

  Result<bool, ErrorCode> handle_flush() override {
    if (!backend) {
      return Result<bool, ErrorCode>::err(GRAPHICS__NOT_INITIALIZED);
    }

    backend->flush();
    return Result<bool, ErrorCode>::ok(true);
  }
};

void proc_graphics(void) {
  Logger l("gfx");
  l.log("Graphics driver starting...");

// Select and initialize backend based on compile-time configuration
#if OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_NONE
  l.log("Using none graphics backend (unimplemented)");
  // Use placement new to avoid static initialization guards
  static char backend_buffer[sizeof(NoneGraphicsBackend)] __attribute__((aligned(alignof(NoneGraphicsBackend))));
  NoneGraphicsBackend *backend_ptr = new (backend_buffer) NoneGraphicsBackend();
  NoneGraphicsBackend &backend = *backend_ptr;
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_TEST
  l.log("Using test graphics backend");
  // Use placement new to avoid static initialization guards
  static char backend_buffer[sizeof(TestGraphicsBackend)] __attribute__((aligned(alignof(TestGraphicsBackend))));
  TestGraphicsBackend *backend_ptr = new (backend_buffer) TestGraphicsBackend();
  TestGraphicsBackend &backend = *backend_ptr;
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_VIRTIO
  l.log("Using VirtIO graphics backend");
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
    l.log("ERROR: No VirtIO GPU device found!");
    ou_exit();
  }

  VirtioGraphicsBackend &backend = *backend_ptr;
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_WASM
  l.log("Using WASM graphics backend");
  // Use placement new to avoid static initialization guards
  static char backend_buffer[sizeof(WasmGraphicsBackend)] __attribute__((aligned(alignof(WasmGraphicsBackend))));
  WasmGraphicsBackend *backend_ptr = new (backend_buffer) WasmGraphicsBackend();
  WasmGraphicsBackend &backend = *backend_ptr;
#else
#error "Unknown graphics backend"
#endif

  if (!backend.init()) {
    l.log("ERROR: Failed to initialize graphics backend");
    ou_exit();
  }

  l.log("Graphics driver initialized successfully");
  l.log("Framebuffer: %ux%u at 0x%lx", backend.get_width(), backend.get_height(), (uintptr_t)backend.get_framebuffer());

  // Create server and set backend pointer
  GraphicsServer server;
  server.backend = &backend;

  // Run IPC server loop
  server.run();
}
