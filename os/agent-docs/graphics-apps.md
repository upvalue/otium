# Graphics System and Application Framework

The OS provides a framebuffer-based graphics system with multi-application support, managed by a graphics server process.

## Architecture

```
┌─────────────────┐              ┌──────────────────────┐
│  Graphics App   │◄────IPC────►│   Graphics Server    │
│  (typedemo,     │              │   (impl.cpp)         │
│   uishell, etc) │              │                      │
└─────────────────┘              │  ┌────────────────┐  │
                                 │  │ Multi-app mgmt │  │
                                 │  │ Taskbar render │  │
                                 │  └────────────────┘  │
                                 │         │            │
                                 │         ▼            │
                                 │  ┌────────────────┐  │
                                 │  │ Backend        │  │
                                 │  │ (virtio/wasm)  │  │
                                 │  └────────────────┘  │
                                 └──────────────────────┘
```

## Multi-Application Support

The graphics server supports up to 10 registered applications. Each app registers with a name and receives an app ID. The most recently registered app is the "active" app and renders to the framebuffer.

### Application Lifecycle

1. **Registration**: App calls `register_app(name)` to register with the graphics server
2. **Render loop**: App calls `should_render()` each frame to check if it's active
3. **Rendering**: If active, app draws to framebuffer and calls `flush()`
4. **Termination**: When app exits, graphics server auto-reaps it on next flush

### IPC API

```cpp
#include "ot/user/gen/graphics-client.hpp"

// Register with graphics server
GraphicsClient gfx(graphics_pid);
auto reg = gfx.register_app("myapp");
if (reg.is_err()) {
  // Too many apps registered
}

// Get framebuffer (height is reduced by taskbar)
auto fb_result = gfx.get_framebuffer();
uint32_t *fb = (uint32_t *)fb_result.value().fb_ptr;
int width = fb_result.value().width;
int height = fb_result.value().height;  // Full height - 28px taskbar

// Main render loop
while (running) {
  auto should = gfx.should_render();
  if (should.is_err() || should.value() == 0) {
    // Not active - yield and try again
    ou_yield();
    continue;
  }

  // Draw to framebuffer
  draw_scene(fb, width, height);

  // Flush to display (also renders taskbar)
  gfx.flush();
  ou_yield();
}
```

### Taskbar

The graphics server renders a 28-pixel taskbar at the bottom of the screen showing all registered apps:

- Format: `[1] appname/pid [2] appname/pid ...`
- Active app is highlighted in bright text
- Uses TTF fonts (Proggy Vector) for clean rendering
- Dark blue-gray background with subtle border

The taskbar is rendered automatically on each `flush()` call, overlaying the app's framebuffer content. Apps receive a reduced height from `get_framebuffer()` so they don't need to account for the taskbar.

### Dead Process Reaping

The graphics server checks if registered processes are still alive before each flush. Dead processes are automatically removed and their app IDs recycled. If the active app dies, the last remaining app becomes active.

## Application Framework

The `app::Framework` class (`ot/lib/app-framework.hpp`) provides utilities for graphics applications.

### Initialization

```cpp
#include "ot/lib/app-framework.hpp"

// Get framebuffer from graphics server
auto fb_result = gfx_client.get_framebuffer();
uint32_t *fb = (uint32_t *)fb_result.value().fb_ptr;
int width = fb_result.value().width;
int height = fb_result.value().height;

// Create framework instance
app::Framework fw(fb, width, height);

// Initialize TTF fonts (requires TLSF memory pool)
auto ttf_result = fw.init_ttf();
if (ttf_result.is_err()) {
  // Fall back to blit16 bitmap font
}
```

### Memory Requirements

The Framework requires a TLSF memory pool for TTF font rendering. Initialize local storage before creating the Framework:

```cpp
struct MyAppStorage : LocalStorage {
  void init() { process_storage_init(20); }  // 20 pages for TTF rendering
};

void my_app_main() {
  void *storage_page = ou_get_storage().as_ptr();
  MyAppStorage *storage = new (storage_page) MyAppStorage();
  storage->init();

  // Now safe to use app::Framework with TTF
}
```

### Drawing Methods

**Primitive operations:**
```cpp
fw.clear(0xFF000000);                    // Clear to black
fw.put_pixel(x, y, color);               // Single pixel
fw.fill_rect(x, y, w, h, color);         // Filled rectangle
fw.draw_hline(x, y, length, color);      // Horizontal line
fw.draw_vline(x, y, length, color);      // Vertical line
fw.draw_gradient_circle(cx, cy, r, center_color, edge_color);
```

**Bitmap font (blit16 - 3x5 pixels):**
```cpp
fw.draw_blit16_text(x, y, "Hello", color, scale);
fw.draw_blit16_char(x, y, 'A', color, scale);
```

**TTF font (Proggy Vector):**
```cpp
// Must call init_ttf() first
fw.draw_ttf_text(x, y, "Hello", color, size_px);
fw.draw_ttf_char(x, y, 'A', color, size_px);
fw.measure_ttf_text("Hello", size_px);  // Get width without drawing
fw.draw_ttf_text_wrapped(x, y, max_width, "Long text...", color, size_px);
```

### Frame Rate Management

Use `graphics::FrameManager` for consistent frame timing:

```cpp
#include "ot/lib/frame-manager.hpp"

graphics::FrameManager fm(30);  // Target 30 FPS

while (running) {
  if (fm.begin_frame()) {
    draw_scene();
    gfx_client.flush();
    fm.end_frame();
  }
  ou_yield();
}
```

## Graphics Server Internals

The graphics server (`ot/user/graphics/impl.cpp`) manages:

1. **Backend selection**: Compile-time selection of graphics backend (virtio, wasm, test, none)
2. **App registry**: Array of `RegisteredApp` structs tracking PID, name, and app_id
3. **Active app tracking**: Index of currently active (rendering) app
4. **Taskbar rendering**: Uses `app::Framework` internally for TTF font rendering

### Memory Setup

The graphics server initializes TLSF via `GraphicsStorage`:

```cpp
struct GraphicsStorage : LocalStorage {
  void init() { process_storage_init(10); }
};

void proc_graphics(void) {
  void *storage_page = ou_get_storage().as_ptr();
  GraphicsStorage *storage = new (storage_page) GraphicsStorage();
  storage->init();
  // ...
}
```

This enables `ou_malloc`/`ou_free` for the Framework's TTF font rendering.

## Pixel Format

All framebuffers use 32-bit BGRA format: `0xAARRGGBB`

- Bits 0-7: Blue
- Bits 8-15: Green
- Bits 16-23: Red
- Bits 24-31: Alpha (usually 0xFF for opaque)

## Files

| File | Description |
|------|-------------|
| `ot/user/graphics/impl.cpp` | Graphics server with multi-app support |
| `ot/user/graphics/backend.hpp` | Backend interface |
| `ot/user/graphics/backend-*.cpp` | Platform-specific backends |
| `ot/lib/app-framework.hpp` | Application framework header |
| `ot/lib/app-framework.cpp` | Framework implementation |
| `ot/lib/frame-manager.hpp` | Frame rate management |
| `ot/lib/font-blit16.hpp` | Bitmap font data |
| `ot/lib/font-proggy.hpp` | TTF font data (embedded) |
| `ot/vendor/libschrift/` | TTF rendering library |
| `services.yaml` | Graphics IPC service definition |
| `ot/user/gen/graphics-*.hpp` | Generated IPC client/server |
