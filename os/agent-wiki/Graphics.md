Otium OS graphics subsystem provides hardware-accelerated rendering through a compositor-based architecture with IPC communication between applications and the graphics server.

# Otium OS Graphics Component

The Otium operating system implements a modern graphics subsystem designed for efficient rendering across different platforms. The graphics component follows a compositor-based architecture where applications communicate with a central graphics server through IPC to perform rendering operations.

## Architecture Overview

The graphics subsystem uses a client-server model with the following key components:

```
┌─────────────────┐     IPC      ┌──────────────────────┐
│  Application    │ ◄──────────► │   Graphics Server    │
│  (AppFramework) │              │   (compositor)       │
└─────────────────┘              └──────────────────────┘
                                          │
                                          ▼
                                 ┌──────────────────────┐
                                 │  Hardware Backend    │
                                 │  (OpenGL/Vulkan)     │
                                 └──────────────────────┘
```

## Graphics Tests

The graphics test suite validates core rendering functionality and compositor behavior:

### Test Categories

1. **Rendering Tests** (`test-render.cpp`)
   - Triangle rendering validation
   - Texture mapping correctness
   - Shader compilation and execution
   - Framebuffer operations

2. **Compositor Tests** (`test-compositor.cpp`)
   - Window creation and destruction
   - Surface composition ordering
   - Alpha blending verification
   - Damage tracking accuracy

3. **Performance Tests** (`test-perf.cpp`)
   - Frame rate benchmarking
   - Memory bandwidth utilization
   - GPU command buffer efficiency

### Running Graphics Tests

```bash
# Run all graphics tests
./otium-test graphics

# Run specific test suite
./otium-test graphics --suite=rendering

# Enable verbose output for debugging
./otium-test graphics --verbose --validate
```

## Build Configuration Options

Graphics-related build options are configured through CMake variables:

### Core Options

```cmake
# Enable graphics subsystem (default: ON)
-DOTIUM_ENABLE_GRAPHICS=ON

# Select graphics backend
-DOTIUM_GRAPHICS_BACKEND=opengl  # Options: opengl, vulkan, software

# Enable validation layers (debug builds)
-DOTIUM_GRAPHICS_VALIDATION=ON

# Set default resolution
-DOTIUM_DEFAULT_WIDTH=1280
-DOTIUM_DEFAULT_HEIGHT=720
```

### Platform-Specific Options

```cmake
# RISC-V platform
-DOTIUM_RISCV_GPU_DRIVER=virtio-gpu

# WebAssembly platform
-DOTIUM_WASM_CANVAS=ON
-DOTIUM_WEBGL_VERSION=2

# Native development
-DOTIUM_USE_SYSTEM_GL=ON
```

### Debug Options

```cmake
# Enable graphics debugging features
-DOTIUM_GRAPHICS_DEBUG=ON

# Shader debugging
-DOTIUM_SHADER_DEBUG=ON
-DOTIUM_SHADER_OPTIMIZATION=OFF

# Frame capture support
-DOTIUM_ENABLE_RENDERDOC=ON
```

## AppFramework: Building Graphical Applications

The AppFramework provides a high-level API for creating graphical applications in Otium:

### Basic Application Structure

```cpp
#include <otium/app/Application.h>
#include <otium/app/Window.h>
#include <otium/graphics/Canvas.h>

class MyApp : public otium::Application {
public:
    void onInit() override {
        // Create main window
        window = createWindow("My Application", 800, 600);

        // Set up rendering context
        canvas = window->getCanvas();
    }

    void onDraw() override {
        canvas->clear(Color::BLACK);

        // Draw a simple rectangle
        canvas->fillRect(100, 100, 200, 150, Color::BLUE);

        // Draw text
        canvas->drawText("Hello, Otium!", 100, 50, Color::WHITE);

        canvas->present();
    }

    void onEvent(const Event& event) override {
        if (event.type == EventType::MouseClick) {
            // Handle mouse input
            handleClick(event.mouse.x, event.mouse.y);
        }
    }

private:
    Window* window;
    Canvas* canvas;
};

// Application entry point
OTIUM_APP_MAIN(MyApp)
```

### Window Management

```cpp
// Create a window with specific properties
WindowConfig config;
config.width = 1024;
config.height = 768;
config.fullscreen = false;
config.resizable = true;
config.title = "Advanced Window";

auto window = createWindow(config);

// Handle window events
window->onResize([](int width, int height) {
    // Adjust rendering viewport
    glViewport(0, 0, width, height);
});

window->onClose([]() {
    // Cleanup before closing
    return true; // Allow close
});
```

### Advanced Rendering

```cpp
// Load and display images
auto texture = canvas->loadTexture("assets/logo.png");
canvas->drawTexture(texture, 10, 10);

// Custom shape rendering
canvas->beginPath();
canvas->moveTo(100, 100);
canvas->lineTo(200, 100);
canvas->lineTo(150, 200);
canvas->closePath();
canvas->fillPath(Color::RED);
canvas->strokePath(Color::WHITE, 2.0f);

// Transformations
canvas->save();
canvas->translate(400, 300);
canvas->rotate(45.0f);
canvas->scale(2.0f, 2.0f);
canvas->drawRect(-50, -50, 100, 100, Color::GREEN);
canvas->restore();
```

## Graphics Server IPC Interface

The graphics server exposes its functionality through a well-defined IPC protocol:

### IPC Message Format

```cpp
struct GraphicsMessage {
    uint32_t type;      // Message type identifier
    uint32_t client_id; // Client process ID
    uint32_t sequence;  // For request/response matching
    uint32_t size;      // Payload size
    uint8_t  payload[]; // Variable-length data
};
```

### Core IPC Operations

#### 1. Surface Management

```cpp
// Create a new surface
struct CreateSurfaceRequest {
    uint32_t width;
    uint32_t height;
    uint32_t format;  // ARGB8888, RGB565, etc.
    uint32_t flags;   // Double-buffered, hardware-accelerated
};

struct CreateSurfaceResponse {
    uint32_t surface_id;
    uint32_t buffer_handle; // Shared memory handle
    uint32_t stride;
};

// Surface operations
enum SurfaceOp {
    SURFACE_CREATE    = 0x1000,
    SURFACE_DESTROY   = 0x1001,
    SURFACE_RESIZE    = 0x1002,
    SURFACE_PRESENT   = 0x1003,
    SURFACE_LOCK      = 0x1004,
    SURFACE_UNLOCK    = 0x1005,
};
```

#### 2. Compositor Commands

```cpp
// Window compositor operations
struct CompositorWindow {
    uint32_t window_id;
    uint32_t surface_id;
    int32_t  x, y;
    uint32_t z_order;
    float    opacity;
    uint32_t flags;
};

enum CompositorOp {
    COMPOSITOR_CREATE_WINDOW  = 0x2000,
    COMPOSITOR_UPDATE_WINDOW  = 0x2001,
    COMPOSITOR_DESTROY_WINDOW = 0x2002,
    COMPOSITOR_SET_VISIBILITY = 0x2003,
    COMPOSITOR_DAMAGE_REGION  = 0x2004,
};
```

#### 3. Rendering Commands

```cpp
// GPU command buffer submission
struct RenderCommand {
    uint32_t target_surface;
    uint32_t command_buffer_handle;
    uint32_t command_count;
    uint32_t fence_id; // For synchronization
};

enum RenderOp {
    RENDER_SUBMIT_COMMANDS = 0x3000,
    RENDER_WAIT_FENCE      = 0x3001,
    RENDER_CREATE_SHADER   = 0x3002,
    RENDER_CREATE_TEXTURE  = 0x3003,
};
```

### IPC Client Library

Applications typically use the client library rather than raw IPC:

```cpp
#include <otium/graphics/GraphicsClient.h>

class GraphicsClient {
public:
    // Connect to graphics server
    bool connect();

    // Surface management
    Surface* createSurface(int width, int height);
    void destroySurface(Surface* surface);

    // Drawing operations
    void beginFrame(Surface* surface);
    void endFrame(Surface* surface);

    // Compositor integration
    Window* createWindow(Surface* surface, const WindowConfig& config);
    void updateWindow(Window* window, const WindowUpdate& update);
};
```

### Shared Memory Protocol

For efficient pixel data transfer, the graphics server uses shared memory:

```cpp
// Shared buffer structure
struct SharedGraphicsBuffer {
    uint32_t magic;      // 'OGFX'
    uint32_t version;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    uint32_t offset;     // Pixel data offset

    // Double buffering support
    uint32_t front_buffer;
    uint32_t back_buffer;
    uint32_t current_buffer;

    // Synchronization
    atomic_uint32_t fence;
    atomic_uint32_t damage_seq;

    // Pixel data follows...
};
```

### Error Handling

The IPC protocol includes comprehensive error reporting:

```cpp
enum GraphicsError {
    GFX_SUCCESS           = 0,
    GFX_ERROR_NO_MEMORY   = -1,
    GFX_ERROR_INVALID_ID  = -2,
    GFX_ERROR_UNSUPPORTED = -3,
    GFX_ERROR_BUSY        = -4,
    GFX_ERROR_TIMEOUT     = -5,
};

struct ErrorResponse {
    int32_t  error_code;
    uint32_t failed_operation;
    char     message[256];
};
```

## Performance Considerations

1. **Batching**: Commands are batched before submission to reduce IPC overhead
2. **Zero-copy**: Shared memory eliminates pixel data copying between processes
3. **Damage tracking**: Only changed regions are recomposited
4. **Hardware acceleration**: Direct GPU access when available
5. **Asynchronous operations**: Non-blocking IPC for smooth animations

## Security Model

The graphics server enforces security through:

- **Surface isolation**: Each client can only access its own surfaces
- **Resource limits**: Per-client memory and surface count limits
- **Input validation**: All IPC messages are validated before processing
- **Capability-based access**: Clients must have graphics capability to connect

## Future Enhancements

Planned improvements to the graphics subsystem include:

- Vulkan backend for modern GPUs
- Hardware video decode acceleration
- Multi-monitor support
- HDR rendering capabilities
- Wayland protocol compatibility layer