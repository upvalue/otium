#include "ot/lib/app-framework.hpp"
#include "ot/lib/logger.hpp"
#include "ot/lib/string-view.hpp"
#include "ot/user/gen/graphics-server.hpp"
#include "ot/user/graphics/backend.hpp"
#include "ot/user/keyboard/backend.hpp"
#include "ot/user/local-storage.hpp"
#include "ot/user/user.hpp"

#if OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_NONE
#include "ot/user/graphics/backend-none.hpp"
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_TEST
#include "ot/user/graphics/backend-test.hpp"
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_VIRTIO
#include "ot/user/graphics/backend-virtio.hpp"
#elif OT_GRAPHICS_BACKEND == OT_GRAPHICS_BACKEND_WASM
#include "ot/user/graphics/backend-wasm.hpp"
#endif

static const int MAX_REGISTERED_APPS = 9;
static const int TASKBAR_HEIGHT = 28;
static const int TASKBAR_FONT_SIZE = 16;
static const uint32_t TASKBAR_BG_COLOR = 0xFF1a1a2e;     // Dark blue-gray
static const uint32_t TASKBAR_BORDER_COLOR = 0xFF2d2d44; // Lighter blue-gray border
static const uint32_t TASKBAR_TEXT_COLOR = 0xFF888899;   // Muted blue-gray text
static const uint32_t TASKBAR_ACTIVE_COLOR = 0xFFccccdd; // Bright text for active app

// Local storage for graphics driver (enables ou_malloc/ou_free)
struct GraphicsStorage : LocalStorage {
  void init() { process_storage_init(10); }
};

struct RegisteredApp {
  bool used;
  Pid pid;
  uint8_t app_id; // 1-based, displayed in taskbar
  char name[16];
};

// Graphics server implementation with instance state
struct GraphicsServer : GraphicsServerBase {
  GraphicsBackend *backend;
  Logger l;
  app::Framework *fw; // For TTF font rendering in taskbar

  // Multi-app state
  RegisteredApp apps[MAX_REGISTERED_APPS];
  int active_app_index;   // Index of most recently registered app (-1 if none)
  uint8_t next_app_id;    // Next ID to assign
  IpcMessage current_msg; // Current message being processed (for sender_pid access)

  GraphicsServer() : backend(nullptr), l("gfx"), fw(nullptr), active_app_index(-1), next_app_id(1) {
    for (int i = 0; i < MAX_REGISTERED_APPS; i++) {
      apps[i].used = false;
    }
  }

  // Initialize the framework for TTF rendering (call after backend is set)
  bool init_framework() {
    if (!backend)
      return false;

    // Use placement new with static buffer to avoid heap allocation for Framework object
    static char fw_buffer[sizeof(app::Framework)] __attribute__((aligned(alignof(app::Framework))));
    fw = new (fw_buffer) app::Framework(backend->get_framebuffer(), backend->get_width(), backend->get_height());

    auto result = fw->init_ttf();
    if (result.is_err()) {
      l.log("Failed to initialize TTF font for taskbar");
      fw = nullptr;
      return false;
    }
    return true;
  }

  // Override run() to store current message before dispatching
  void run() {
    while (true) {
      current_msg = ou_ipc_recv();
      process_request(current_msg);
    }
  }

  // Helper to get sender PID from current message
  Pid sender_pid() const { return current_msg.sender_pid; }

  // Find app slot by PID, returns -1 if not found
  int find_app_by_pid(Pid pid) {
    for (int i = 0; i < MAX_REGISTERED_APPS; i++) {
      if (apps[i].used && apps[i].pid == pid) {
        return i;
      }
    }
    return -1;
  }

  // Find app slot by app_id (1-based), returns -1 if not found
  int find_app_by_id(uint8_t app_id) {
    for (int i = 0; i < MAX_REGISTERED_APPS; i++) {
      if (apps[i].used && apps[i].app_id == app_id) {
        return i;
      }
    }
    return -1;
  }

  // Reap dead processes and renumber app_ids
  void reap_dead_processes() {
    bool any_reaped = false;

    for (int i = 0; i < MAX_REGISTERED_APPS; i++) {
      if (apps[i].used && !ou_proc_is_alive(apps[i].pid)) {
        l.log("Reaping dead app: %s (pid=%lu)", apps[i].name, apps[i].pid.raw());
        apps[i].used = false;
        any_reaped = true;

        // If this was the active app, we need to find a new one
        if (active_app_index == i) {
          active_app_index = -1;
        }
      }
    }

    if (any_reaped) {
      // Renumber remaining apps sequentially and find new active
      uint8_t new_id = 1;
      int last_used_index = -1;
      for (int i = 0; i < MAX_REGISTERED_APPS; i++) {
        if (apps[i].used) {
          apps[i].app_id = new_id++;
          last_used_index = i;
        }
      }
      next_app_id = new_id;

      // If we lost the active app, make the last registered one active
      if (active_app_index == -1 && last_used_index >= 0) {
        active_app_index = last_used_index;
      }

      // If no apps remain, show idle screen
      if (last_used_index < 0) {
        render_idle_screen();
      }
    }
  }

  // Render the taskbar at the bottom of the screen
  void render_taskbar() {
    if (!backend)
      return;

    uint32_t *fb = backend->get_framebuffer();
    int width = backend->get_width();
    int height = backend->get_height();
    int taskbar_y = height - TASKBAR_HEIGHT;

    // Fill taskbar background
    for (int y = taskbar_y; y < height; y++) {
      for (int x = 0; x < width; x++) {
        fb[y * width + x] = TASKBAR_BG_COLOR;
      }
    }

    // Draw top border (1px lighter line)
    for (int x = 0; x < width; x++) {
      fb[taskbar_y * width + x] = TASKBAR_BORDER_COLOR;
    }

    // Draw registered apps using TTF font if available
    if (fw && fw->ttf_available()) {
      int text_x = 12;
      int text_y = taskbar_y + 5; // Vertically center for 16px font in 28px bar
      char buf[32];

      for (int i = 0; i < MAX_REGISTERED_APPS; i++) {
        if (apps[i].used) {
          // Format: [id] name/pid
          int len = 0;
          buf[len++] = '[';
          if (apps[i].app_id >= 10) {
            buf[len++] = '0' + (apps[i].app_id / 10);
          }
          buf[len++] = '0' + (apps[i].app_id % 10);
          buf[len++] = ']';
          buf[len++] = ' ';

          // Copy name
          for (int j = 0; apps[i].name[j] && j < 12 && len < 28; j++) {
            buf[len++] = apps[i].name[j];
          }

          buf[len++] = '/';

          // Add pid (just the raw number, keep it short)
          uintptr_t pid_val = apps[i].pid.raw();
          if (pid_val >= 100) {
            buf[len++] = '0' + ((pid_val / 100) % 10);
          }
          if (pid_val >= 10) {
            buf[len++] = '0' + ((pid_val / 10) % 10);
          }
          buf[len++] = '0' + (pid_val % 10);

          buf[len] = '\0';

          // Highlight active app
          uint32_t color = (i == active_app_index) ? TASKBAR_ACTIVE_COLOR : TASKBAR_TEXT_COLOR;
          auto result = fw->draw_ttf_text(text_x, text_y, buf, color, TASKBAR_FONT_SIZE);
          if (result.is_ok()) {
            text_x += result.value() + 20; // Add spacing between apps
          }
        }
      }
    }
  }

  // Count how many apps are currently registered
  int count_active_apps() {
    int count = 0;
    for (int i = 0; i < MAX_REGISTERED_APPS; i++) {
      if (apps[i].used) {
        count++;
      }
    }
    return count;
  }

  // Render idle screen when no apps are running
  void render_idle_screen() {
    if (!backend)
      return;

    uint32_t *fb = backend->get_framebuffer();
    int width = backend->get_width();
    int height = backend->get_height();

    // Clear to dark background
    for (int i = 0; i < width * height; i++) {
      fb[i] = TASKBAR_BG_COLOR;
    }

    // Render centered "No apps running" message
    if (fw && fw->ttf_available()) {
      const char *msg = "No apps running";
      auto measure = fw->measure_ttf_text(msg, 20);
      if (measure.is_ok()) {
        int text_x = (width - measure.value()) / 2;
        int text_y = (height - TASKBAR_HEIGHT) / 2;
        fw->draw_ttf_text(text_x, text_y, msg, 0xFF666666, 20);
      }
    }

    // Render empty taskbar for consistent UI
    render_taskbar();

    backend->flush();
  }

  Result<GetFramebufferResult, ErrorCode> handle_get_framebuffer() override {
    if (!backend || !backend->get_framebuffer()) {
      return Result<GetFramebufferResult, ErrorCode>::err(GRAPHICS__NOT_INITIALIZED);
    }

    GetFramebufferResult result;
    result.fb_ptr = (uintptr_t)backend->get_framebuffer();
    result.width = backend->get_width();
    // Return reduced height to keep apps above the taskbar
    result.height = backend->get_height() - TASKBAR_HEIGHT;

    l.log("Returning fb_ptr=0x%lx, width=%lu, height=%lu", result.fb_ptr, result.width, result.height);

    return Result<GetFramebufferResult, ErrorCode>::ok(result);
  }

  Result<bool, ErrorCode> handle_flush() override {
    if (!backend) {
      return Result<bool, ErrorCode>::err(GRAPHICS__NOT_INITIALIZED);
    }

    // Reap dead processes before rendering
    reap_dead_processes();

    // Render taskbar over the app content
    render_taskbar();

    backend->flush();
    return Result<bool, ErrorCode>::ok(true);
  }

  Result<uintptr_t, ErrorCode> handle_register_app(const StringView &name) override {
    // Find a free slot
    int slot = -1;
    for (int i = 0; i < MAX_REGISTERED_APPS; i++) {
      if (!apps[i].used) {
        slot = i;
        break;
      }
    }

    if (slot < 0) {
      return Result<uintptr_t, ErrorCode>::err(GRAPHICS__TOO_MANY_APPS);
    }

    // Register the app
    apps[slot].used = true;
    apps[slot].pid = sender_pid();
    apps[slot].app_id = next_app_id++;

    // Copy name (truncate if needed)
    size_t name_len = name.len < 15 ? name.len : 15;
    for (size_t i = 0; i < name_len; i++) {
      apps[slot].name[i] = name.ptr[i];
    }
    apps[slot].name[name_len] = '\0';

    // Most recently registered app becomes active
    active_app_index = slot;

    l.log("Registered app: %s (pid=%lu, app_id=%d)", apps[slot].name, apps[slot].pid.raw(), apps[slot].app_id);

    return Result<uintptr_t, ErrorCode>::ok(apps[slot].app_id);
  }

  Result<uintptr_t, ErrorCode> handle_should_render() override {
    int slot = find_app_by_pid(sender_pid());
    if (slot < 0) {
      return Result<uintptr_t, ErrorCode>::err(GRAPHICS__NOT_REGISTERED);
    }

    // Return 1 if this app is active, 0 otherwise
    uintptr_t active = (slot == active_app_index) ? 1 : 0;
    return Result<uintptr_t, ErrorCode>::ok(active);
  }

  Result<bool, ErrorCode> handle_unregister_app() override {
    int slot = find_app_by_pid(sender_pid());
    if (slot < 0) {
      return Result<bool, ErrorCode>::err(GRAPHICS__NOT_REGISTERED);
    }

    l.log("Unregistering app: %s (pid=%lu)", apps[slot].name, apps[slot].pid.raw());
    apps[slot].used = false;

    // If this was the active app, clear it
    if (active_app_index == slot) {
      active_app_index = -1;
    }

    // Renumber remaining apps sequentially and find new active
    uint8_t new_id = 1;
    int last_used_index = -1;
    for (int i = 0; i < MAX_REGISTERED_APPS; i++) {
      if (apps[i].used) {
        apps[i].app_id = new_id++;
        last_used_index = i;
      }
    }
    next_app_id = new_id;

    // If we lost the active app, make the last registered one active
    if (active_app_index == -1 && last_used_index >= 0) {
      active_app_index = last_used_index;
    }

    // If no apps remain, show idle screen
    int remaining = count_active_apps();
    l.log("After unregister: %d apps remaining", remaining);
    if (remaining == 0) {
      l.log("No apps remaining, rendering idle screen");
      render_idle_screen();
    }

    return Result<bool, ErrorCode>::ok(true);
  }

  Result<uintptr_t, ErrorCode> handle_handle_key(uintptr_t code, uintptr_t flags) override {

    // Only handle key press events
    if (!(flags & KEY_FLAG_PRESSED)) {
      return Result<uintptr_t, ErrorCode>::ok(0);
    }

    // Check for Alt+1-9 to switch apps
    if (flags & KEY_FLAG_ALT) {
      if (code >= KEY_1 && code <= KEY_9) {
        uint8_t target_app_id = (uint8_t)(code - KEY_1 + 1);
        int slot = find_app_by_id(target_app_id);
        if (slot >= 0) {
          active_app_index = slot;
          l.log("Switched to app %d: %s (pid=%lu)", target_app_id, apps[slot].name, apps[slot].pid.raw());
        }
        return Result<uintptr_t, ErrorCode>::ok(1); // consumed
      }
    }

    return Result<uintptr_t, ErrorCode>::ok(0); // not consumed
  }
};

void proc_graphics(void) {
  // Initialize local storage for malloc/free support
  // Use the kernel-provided storage page so local_storage is correct after context switches
  void *storage_page = ou_get_storage().as_ptr();
  GraphicsStorage *storage = new (storage_page) GraphicsStorage();
  storage->init();

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

  Result<uintptr_t, ErrorCode> result = VirtIODevice::scan_for_device(VIRTIO_ID_GPU);
  if (result.is_err()) {
    l.log("ERROR: No VirtIO GPU device found!");
    ou_exit();
  }

  backend_ptr = new (backend_buffer) VirtioGraphicsBackend(result.value());

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

  // Initialize framework for TTF taskbar rendering
  if (!server.init_framework()) {
    l.log("WARNING: TTF fonts not available for taskbar");
  }

  // Run IPC server loop
  server.run();
}
