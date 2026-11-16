// The WebGPU backend for the UI renderer, through webgpu.h (wgpu-native on
// desktop). The driver names a WebGPU backend (vulkan, metal, gl, d3d12) or
// is empty for the default.
#include <SDL3/SDL.h>
#include <SDL3/SDL_metal.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>
#include "renderer.h"
static const char rect_wgsl[] = {
#embed "shaders/rect.wgsl" suffix(, 0)
};
static const char copy_wgsl[] = {
#embed "shaders/copy.wgsl" suffix(, 0)
};

#define OFFSCREEN_FORMAT WGPUTextureFormat_RGBA8Unorm

struct Gpu {
  SDL_Window* window;
  SDL_MetalView metal_view;
  bool offscreen;
  WGPUInstance instance;
  WGPUSurface surface;
  WGPUAdapter adapter;
  WGPUDevice device;
  WGPUQueue queue;
  // The surface is configured on the first visible frame and again when the
  // window's pixel size changes.
  WGPUTextureFormat surface_format;
  WGPUCompositeAlphaMode alpha_mode;
  Uint32 surface_width, surface_height;
  WGPUBindGroupLayout rect_layout;
  WGPUPipelineLayout rect_pipeline_layout;
  WGPUShaderModule rect_module, copy_module;
  // Rect pipelines for the offscreen and surface formats, and the copy to
  // the surface.
  WGPURenderPipeline rect_offscreen, rect_surface, copy;
  WGPUBuffer uniforms, instances;
  Uint32 capacity;
  WGPUTexture atlas;
  WGPUTextureView atlas_view;
  WGPUSampler sampler;
  WGPUBindGroup rect_group;
  // The offscreen target, also drawn into for captures in direct mode, and
  // the copy's bind group for it.
  WGPUTexture target;
  WGPUTextureView target_view;
  WGPUBindGroup copy_group;
  Uint32 target_width, target_height;
  Uint32 width, height;
  char driver[64];
  // The first error the device reported since the last check.
  char error[512];
  bool failed;
};

static WGPUStringView text(const char* s) { return (WGPUStringView){s, WGPU_STRLEN}; }

static void on_error(WGPUDevice const* device, WGPUErrorType type, WGPUStringView message,
                     void* userdata, void* unused) {
  (void)device;
  (void)type;
  (void)unused;
  Gpu* g = userdata;
  if (g->failed) return;
  g->failed = true;
  size_t n = message.length == WGPU_STRLEN ? strlen(message.data) : message.length;
  if (n >= sizeof(g->error)) n = sizeof(g->error) - 1;
  memcpy(g->error, message.data ? message.data : "", message.data ? n : 0);
  g->error[message.data ? n : 0] = 0;
}

// Report and clear an error the device raised during the last calls.
static bool check(Gpu* g) {
  if (!g->failed) return true;
  g->failed = false;
  return SDL_SetError("WebGPU: %s", g->error);
}

typedef struct {
  bool done;
  void* result;
  char message[256];
} Request;

static void finish(Request* r, void* result, WGPUStringView message) {
  r->done = true;
  r->result = result;
  if (!result && message.data) {
    size_t n = message.length == WGPU_STRLEN ? strlen(message.data) : message.length;
    if (n >= sizeof(r->message)) n = sizeof(r->message) - 1;
    memcpy(r->message, message.data, n);
    r->message[n] = 0;
  }
}

static void on_adapter(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message,
                       void* userdata, void* unused) {
  (void)unused;
  finish(userdata, status == WGPURequestAdapterStatus_Success ? adapter : NULL, message);
}

static void on_device(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message,
                      void* userdata, void* unused) {
  (void)unused;
  finish(userdata, status == WGPURequestDeviceStatus_Success ? device : NULL, message);
}

static void on_map(WGPUMapAsyncStatus status, WGPUStringView message, void* userdata,
                   void* unused) {
  (void)unused;
  finish(userdata, status == WGPUMapAsyncStatus_Success ? userdata : NULL, message);
}

// wgpu-native completes requests during the call or in ProcessEvents.
static bool wait_for(Gpu* g, Request* r) {
  for (int i = 0; !r->done && i < 10000; ++i) {
    wgpuInstanceProcessEvents(g->instance);
    if (!r->done) SDL_Delay(1);
  }
  return r->done;
}

static bool backend_type(const char* name, WGPUBackendType* type) {
  static const struct {
    const char* name;
    WGPUBackendType type;
  } names[] = {{"", WGPUBackendType_Undefined},  {"vulkan", WGPUBackendType_Vulkan},
               {"metal", WGPUBackendType_Metal}, {"gl", WGPUBackendType_OpenGL},
               {"d3d12", WGPUBackendType_D3D12}, {"d3d11", WGPUBackendType_D3D11}};

  for (size_t i = 0; i < SDL_arraysize(names); ++i)
    if (strcmp(name, names[i].name) == 0) {
      *type = names[i].type;
      return true;
    }
  return SDL_SetError("Unknown WebGPU backend '%s'; expected vulkan, metal, gl, d3d12 or d3d11",
                      name);
}

static const char* backend_name(WGPUBackendType type) {
  switch (type) {
    case WGPUBackendType_Vulkan: return "vulkan";
    case WGPUBackendType_Metal: return "metal";
    case WGPUBackendType_OpenGL: return "gl";
    case WGPUBackendType_OpenGLES: return "gles";
    case WGPUBackendType_D3D12: return "d3d12";
    case WGPUBackendType_D3D11: return "d3d11";
    default: return "unknown";
  }
}

static bool make_surface(Gpu* g) {
  WGPUSurfaceDescriptor desc = {0};
#ifdef __APPLE__
  g->metal_view = SDL_Metal_CreateView(g->window);
  if (!g->metal_view) return false;
  WGPUSurfaceSourceMetalLayer source = {.chain.sType = WGPUSType_SurfaceSourceMetalLayer,
                                        .layer = SDL_Metal_GetLayer(g->metal_view)};
  desc.nextInChain = &source.chain;
  g->surface = wgpuInstanceCreateSurface(g->instance, &desc);
#else
  SDL_PropertiesID props = SDL_GetWindowProperties(g->window);
  const char* video = SDL_GetCurrentVideoDriver();
  if (video && strcmp(video, "wayland") == 0) {
    WGPUSurfaceSourceWaylandSurface source = {
        .chain.sType = WGPUSType_SurfaceSourceWaylandSurface,
        .display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL),
        .surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL)};
    desc.nextInChain = &source.chain;
    g->surface = wgpuInstanceCreateSurface(g->instance, &desc);
  } else if (video && strcmp(video, "x11") == 0) {
    WGPUSurfaceSourceXlibWindow source = {
        .chain.sType = WGPUSType_SurfaceSourceXlibWindow,
        .display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL),
        .window = (uint64_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)};
    desc.nextInChain = &source.chain;
    g->surface = wgpuInstanceCreateSurface(g->instance, &desc);
  } else {
    return SDL_SetError("WebGPU needs a Wayland or X11 window, not '%s'", video ? video : "none");
  }
#endif
  return g->surface ? true : SDL_SetError("Could not create a WebGPU surface");
}

static void release_target(Gpu* g) {
  if (g->copy_group) wgpuBindGroupRelease(g->copy_group);
  if (g->target_view) wgpuTextureViewRelease(g->target_view);
  if (g->target) wgpuTextureRelease(g->target);
  g->copy_group = NULL;
  g->target_view = NULL;
  g->target = NULL;
  g->target_width = g->target_height = 0;
}

static void gpu_free(Gpu* g) {
  if (!g) return;
  release_target(g);
  if (g->rect_group) wgpuBindGroupRelease(g->rect_group);
  if (g->sampler) wgpuSamplerRelease(g->sampler);
  if (g->atlas_view) wgpuTextureViewRelease(g->atlas_view);
  if (g->atlas) wgpuTextureRelease(g->atlas);
  if (g->instances) wgpuBufferRelease(g->instances);
  if (g->uniforms) wgpuBufferRelease(g->uniforms);
  if (g->copy) wgpuRenderPipelineRelease(g->copy);
  if (g->rect_surface) wgpuRenderPipelineRelease(g->rect_surface);
  if (g->rect_offscreen) wgpuRenderPipelineRelease(g->rect_offscreen);
  if (g->copy_module) wgpuShaderModuleRelease(g->copy_module);
  if (g->rect_module) wgpuShaderModuleRelease(g->rect_module);
  if (g->rect_pipeline_layout) wgpuPipelineLayoutRelease(g->rect_pipeline_layout);
  if (g->rect_layout) wgpuBindGroupLayoutRelease(g->rect_layout);
  if (g->queue) wgpuQueueRelease(g->queue);
  if (g->surface && g->surface_width) wgpuSurfaceUnconfigure(g->surface);
  if (g->device) wgpuDeviceRelease(g->device);
  if (g->adapter) wgpuAdapterRelease(g->adapter);
  if (g->surface) wgpuSurfaceRelease(g->surface);
  if (g->instance) wgpuInstanceRelease(g->instance);
  if (g->metal_view) SDL_Metal_DestroyView(g->metal_view);
  free(g);
}

static WGPUShaderModule shader(Gpu* g, const char* source) {
  WGPUShaderSourceWGSL wgsl = {.chain.sType = WGPUSType_ShaderSourceWGSL, .code = text(source)};
  WGPUShaderModuleDescriptor desc = {.nextInChain = &wgsl.chain};
  return wgpuDeviceCreateShaderModule(g->device, &desc);
}

static WGPURenderPipeline rect_pipeline(Gpu* g, WGPUTextureFormat format) {
  WGPUVertexAttribute attributes[5];
  for (Uint32 i = 0; i < 5; ++i)
    attributes[i] = (WGPUVertexAttribute){
        .format = WGPUVertexFormat_Float32x4, .offset = i * 16, .shaderLocation = i};
  WGPUVertexBufferLayout instances = {.stepMode = WGPUVertexStepMode_Instance,
                                      .arrayStride = sizeof(Rect),
                                      .attributeCount = 5,
                                      .attributes = attributes};
  // Premultiplied alpha, as the SDL_GPU backend blends.
  WGPUBlendComponent over = {.operation = WGPUBlendOperation_Add,
                             .srcFactor = WGPUBlendFactor_One,
                             .dstFactor = WGPUBlendFactor_OneMinusSrcAlpha};
  WGPUBlendState blend = {.color = over, .alpha = over};
  WGPUColorTargetState target = {
      .format = format, .blend = &blend, .writeMask = WGPUColorWriteMask_All};
  WGPUFragmentState fragment = {.module = g->rect_module,
                                .entryPoint = text("rect_fragment"),
                                .targetCount = 1,
                                .targets = &target};
  WGPURenderPipelineDescriptor desc = {
      .layout = g->rect_pipeline_layout,
      .vertex = {.module = g->rect_module,
                 .entryPoint = text("rect_vertex"),
                 .bufferCount = 1,
                 .buffers = &instances},
      .primitive = {.topology = WGPUPrimitiveTopology_TriangleList},
      .multisample = {.count = 1, .mask = UINT32_MAX},
      .fragment = &fragment,
  };
  return wgpuDeviceCreateRenderPipeline(g->device, &desc);
}

static Gpu* gpu_new(SDL_Window* window, const char* driver, bool offscreen) {
  WGPUBackendType type = WGPUBackendType_Undefined;
  if (!backend_type(driver, &type)) return NULL;
  Gpu* g = calloc(1, sizeof(*g));
  if (!g) {
    SDL_OutOfMemory();
    return NULL;
  }
  g->window = window;
  g->offscreen = offscreen;
  g->instance = wgpuCreateInstance(NULL);
  if (!g->instance) {
    SDL_SetError("Could not create a WebGPU instance");
    goto failed;
  }
  if (!make_surface(g)) goto failed;
  Request adapter = {0};
  WGPURequestAdapterOptions options = {.compatibleSurface = g->surface, .backendType = type};
  wgpuInstanceRequestAdapter(
      g->instance, &options,
      (WGPURequestAdapterCallbackInfo){.mode = WGPUCallbackMode_AllowProcessEvents,
                                       .callback = on_adapter,
                                       .userdata1 = &adapter});
  if (!wait_for(g, &adapter) || !adapter.result) {
    SDL_SetError("No WebGPU adapter: %s", adapter.message);
    goto failed;
  }
  g->adapter = adapter.result;
  WGPUAdapterInfo info = {0};
  if (wgpuAdapterGetInfo(g->adapter, &info) == WGPUStatus_Success) {
    SDL_snprintf(g->driver, sizeof(g->driver), "webgpu/%s", backend_name(info.backendType));
    wgpuAdapterInfoFreeMembers(info);
  } else {
    SDL_strlcpy(g->driver, "webgpu", sizeof(g->driver));
  }
  Request device = {0};
  WGPUDeviceDescriptor device_desc = {
      .uncapturedErrorCallbackInfo = {.callback = on_error, .userdata1 = g}};
  wgpuAdapterRequestDevice(
      g->adapter, &device_desc,
      (WGPURequestDeviceCallbackInfo){.mode = WGPUCallbackMode_AllowProcessEvents,
                                      .callback = on_device,
                                      .userdata1 = &device});
  if (!wait_for(g, &device) || !device.result) {
    SDL_SetError("No WebGPU device: %s", device.message);
    goto failed;
  }
  g->device = device.result;
  g->queue = wgpuDeviceGetQueue(g->device);

  WGPUBindGroupLayoutEntry entries[3] = {
      {.binding = 0,
       .visibility = WGPUShaderStage_Vertex,
       .buffer = {.type = WGPUBufferBindingType_Uniform, .minBindingSize = 16}},
      {.binding = 1,
       .visibility = WGPUShaderStage_Fragment,
       .texture = {.sampleType = WGPUTextureSampleType_Float,
                   .viewDimension = WGPUTextureViewDimension_2D}},
      {.binding = 2,
       .visibility = WGPUShaderStage_Fragment,
       .sampler = {.type = WGPUSamplerBindingType_Filtering}},
  };
  g->rect_layout = wgpuDeviceCreateBindGroupLayout(
      g->device, &(WGPUBindGroupLayoutDescriptor){.entryCount = 3, .entries = entries});
  g->rect_pipeline_layout = wgpuDeviceCreatePipelineLayout(
      g->device, &(WGPUPipelineLayoutDescriptor){.bindGroupLayoutCount = 1,
                                                 .bindGroupLayouts = &g->rect_layout});
  g->rect_module = shader(g, rect_wgsl);
  g->copy_module = shader(g, copy_wgsl);
  g->rect_offscreen = rect_pipeline(g, OFFSCREEN_FORMAT);
  g->uniforms = wgpuDeviceCreateBuffer(
      g->device, &(WGPUBufferDescriptor){.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst,
                                         .size = 16});
  g->atlas = wgpuDeviceCreateTexture(
      g->device,
      &(WGPUTextureDescriptor){.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst,
                               .dimension = WGPUTextureDimension_2D,
                               .size = {ATLAS_SIZE, ATLAS_SIZE, 1},
                               .format = WGPUTextureFormat_R8Unorm,
                               .mipLevelCount = 1,
                               .sampleCount = 1});
  if (!check(g) || !g->atlas) goto failed;
  g->atlas_view = wgpuTextureCreateView(g->atlas, NULL);
  g->sampler = wgpuDeviceCreateSampler(
      g->device, &(WGPUSamplerDescriptor){.addressModeU = WGPUAddressMode_ClampToEdge,
                                          .addressModeV = WGPUAddressMode_ClampToEdge,
                                          .addressModeW = WGPUAddressMode_ClampToEdge,
                                          .magFilter = WGPUFilterMode_Linear,
                                          .minFilter = WGPUFilterMode_Linear,
                                          .mipmapFilter = WGPUMipmapFilterMode_Nearest,
                                          .lodMaxClamp = 32,
                                          .maxAnisotropy = 1});
  WGPUBindGroupEntry bindings[3] = {
      {.binding = 0, .buffer = g->uniforms, .size = 16},
      {.binding = 1, .textureView = g->atlas_view},
      {.binding = 2, .sampler = g->sampler},
  };
  g->rect_group = wgpuDeviceCreateBindGroup(
      g->device,
      &(WGPUBindGroupDescriptor){.layout = g->rect_layout, .entryCount = 3, .entries = bindings});
  if (!check(g) || !g->rect_module || !g->copy_module || !g->rect_offscreen || !g->uniforms ||
      !g->atlas_view || !g->sampler || !g->rect_group)
    goto failed;
  return g;
failed:
  {
    char message[1024];
    SDL_strlcpy(message, SDL_GetError(), sizeof(message));
    gpu_free(g);
    SDL_SetError("%s", message);
    return NULL;
  }
}

static const char* gpu_driver(Gpu* g) { return g->driver; }

// The target is made when a frame needs it, at the size recorded here.
static bool gpu_resize(Gpu* g, Uint32 w, Uint32 h) {
  g->width = w;
  g->height = h;
  return true;
}

static bool ensure_target(Gpu* g) {
  if (g->target && g->target_width == g->width && g->target_height == g->height) return true;
  WGPUTexture target = wgpuDeviceCreateTexture(
      g->device,
      &(WGPUTextureDescriptor){.usage = WGPUTextureUsage_RenderAttachment |
                                        WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopySrc,
                               .dimension = WGPUTextureDimension_2D,
                               .size = {g->width, g->height, 1},
                               .format = OFFSCREEN_FORMAT,
                               .mipLevelCount = 1,
                               .sampleCount = 1});
  if (!check(g) || !target) {
    if (target) wgpuTextureRelease(target);
    return false;
  }
  release_target(g);
  g->target = target;
  g->target_view = wgpuTextureCreateView(target, NULL);
  g->target_width = g->width;
  g->target_height = g->height;
  return check(g);
}

static bool gpu_reserve(Gpu* g, Uint32 capacity) {
  if (capacity <= g->capacity) return true;
  WGPUBuffer buffer = wgpuDeviceCreateBuffer(
      g->device, &(WGPUBufferDescriptor){.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst,
                                         .size = (uint64_t)capacity * sizeof(Rect)});
  if (!check(g) || !buffer) {
    if (buffer) wgpuBufferRelease(buffer);
    return false;
  }
  if (g->instances) wgpuBufferRelease(g->instances);
  g->instances = buffer;
  g->capacity = capacity;
  return true;
}

// The atlas is a texture; writes go through the queue's own staging.
static long long gpu_memory(Gpu* g, int kind) {
  switch (kind) {
    case 0: return ATLAS_BYTES;
    case 1: return (long long)g->capacity * sizeof(Rect);
    case 3: return g->offscreen ? (long long)g->target_width * g->target_height * 4 : 0;
    default: return 0;
  }
}

// Configure the surface for the window's pixel size, choosing an 8-bit
// non-sRGB format as SDL_GPU's default swapchain is.
static bool configure(Gpu* g, Uint32 w, Uint32 h) {
  if (!g->surface_format) {
    WGPUSurfaceCapabilities caps = {0};
    if (wgpuSurfaceGetCapabilities(g->surface, g->adapter, &caps) != WGPUStatus_Success ||
        !caps.formatCount)
      return SDL_SetError("The WebGPU surface reports no formats");
    g->surface_format = caps.formats[0];
    for (size_t i = 0; i < caps.formatCount; ++i)
      if (caps.formats[i] == WGPUTextureFormat_BGRA8Unorm ||
          caps.formats[i] == WGPUTextureFormat_RGBA8Unorm) {
        g->surface_format = caps.formats[i];
        break;
      }
    g->alpha_mode = caps.alphaModeCount ? caps.alphaModes[0] : WGPUCompositeAlphaMode_Auto;
    for (size_t i = 0; i < caps.alphaModeCount; ++i)
      if (caps.alphaModes[i] == WGPUCompositeAlphaMode_Opaque) g->alpha_mode = caps.alphaModes[i];
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    g->rect_surface =
        g->surface_format == OFFSCREEN_FORMAT ? NULL : rect_pipeline(g, g->surface_format);
    WGPUColorTargetState target = {.format = g->surface_format,
                                   .writeMask = WGPUColorWriteMask_All};
    WGPUFragmentState fragment = {.module = g->copy_module,
                                  .entryPoint = text("copy_fragment"),
                                  .targetCount = 1,
                                  .targets = &target};
    g->copy = wgpuDeviceCreateRenderPipeline(
        g->device, &(WGPURenderPipelineDescriptor){
                       .vertex = {.module = g->copy_module, .entryPoint = text("copy_vertex")},
                       .primitive = {.topology = WGPUPrimitiveTopology_TriangleList},
                       .multisample = {.count = 1, .mask = UINT32_MAX},
                       .fragment = &fragment});
    if (!check(g) || !g->copy) return false;
  }
  wgpuSurfaceConfigure(g->surface,
                       &(WGPUSurfaceConfiguration){.device = g->device,
                                                   .format = g->surface_format,
                                                   .usage = WGPUTextureUsage_RenderAttachment,
                                                   .width = w,
                                                   .height = h,
                                                   .alphaMode = g->alpha_mode,
                                                   .presentMode = WGPUPresentMode_Fifo});
  g->surface_width = w;
  g->surface_height = h;
  return check(g);
}

// The window's current surface texture, or NULL with no error when it has
// none to draw to: hidden, minimized or timed out.
static bool acquire(Gpu* g, WGPUTexture* texture) {
  *texture = NULL;
  if (SDL_GetWindowFlags(g->window) & (SDL_WINDOW_HIDDEN | SDL_WINDOW_MINIMIZED)) return true;
  int w, h;
  if (!SDL_GetWindowSizeInPixels(g->window, &w, &h)) return false;
  if (w <= 0 || h <= 0) return true;
  for (int attempt = 0; attempt < 2; ++attempt) {
    if ((Uint32)w != g->surface_width || (Uint32)h != g->surface_height || attempt)
      if (!configure(g, (Uint32)w, (Uint32)h)) return false;
    WGPUSurfaceTexture current = {0};
    wgpuSurfaceGetCurrentTexture(g->surface, &current);
    switch (current.status) {
      case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal:
      case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal:
        *texture = current.texture;
        return true;
      case WGPUSurfaceGetCurrentTextureStatus_Timeout: return true;
      case WGPUSurfaceGetCurrentTextureStatus_Outdated:
      case WGPUSurfaceGetCurrentTextureStatus_Lost:
        if (current.texture) wgpuTextureRelease(current.texture);
        continue;
      default:
        if (current.texture) wgpuTextureRelease(current.texture);
        return SDL_SetError("Could not get the WebGPU surface texture (%d)", (int)current.status);
    }
  }
  return true;
}

static void draw(Gpu* g, const GpuFrame* f, WGPUCommandEncoder encoder, WGPUTextureView view,
                 WGPURenderPipeline pipeline) {
  WGPURenderPassColorAttachment color = {
      .view = view,
      .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
      .loadOp = WGPULoadOp_Clear,
      .storeOp = WGPUStoreOp_Store,
      .clearValue = {f->clear.r, f->clear.g, f->clear.b, f->clear.a}};
  WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
      encoder, &(WGPURenderPassDescriptor){.colorAttachmentCount = 1, .colorAttachments = &color});
  if (f->count) {
    wgpuRenderPassEncoderSetPipeline(pass, pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, g->rect_group, 0, NULL);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, g->instances, 0,
                                         (uint64_t)f->count * sizeof(Rect));
    wgpuRenderPassEncoderDraw(pass, 6, f->count, 0, 0);
  }
  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass);
}

// Stretch the target over the surface texture, as SDL_GPU's blit does.
static bool copy_to(Gpu* g, WGPUCommandEncoder encoder, WGPUTextureView view) {
  if (!g->copy_group) {
    WGPUBindGroupEntry bindings[2] = {{.binding = 0, .textureView = g->target_view},
                                      {.binding = 1, .sampler = g->sampler}};
    WGPUBindGroupLayout layout = wgpuRenderPipelineGetBindGroupLayout(g->copy, 0);
    g->copy_group = wgpuDeviceCreateBindGroup(
        g->device,
        &(WGPUBindGroupDescriptor){.layout = layout, .entryCount = 2, .entries = bindings});
    wgpuBindGroupLayoutRelease(layout);
    if (!check(g) || !g->copy_group) return false;
  }
  WGPURenderPassColorAttachment color = {.view = view,
                                         .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
                                         .loadOp = WGPULoadOp_Clear,
                                         .storeOp = WGPUStoreOp_Store};
  WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(
      encoder, &(WGPURenderPassDescriptor){.colorAttachmentCount = 1, .colorAttachments = &color});
  wgpuRenderPassEncoderSetPipeline(pass, g->copy);
  wgpuRenderPassEncoderSetBindGroup(pass, 0, g->copy_group, 0, NULL);
  wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
  wgpuRenderPassEncoderEnd(pass);
  wgpuRenderPassEncoderRelease(pass);
  return true;
}

// Wait for the readback, then save it with rows packed tightly.
static bool save(Gpu* g, WGPUBuffer readback, Uint32 stride, const char* path) {
  Uint32 size = stride * g->target_height;
  Request mapped = {0};
  wgpuBufferMapAsync(readback, WGPUMapMode_Read, 0, size,
                     (WGPUBufferMapCallbackInfo){.mode = WGPUCallbackMode_AllowProcessEvents,
                                                 .callback = on_map,
                                                 .userdata1 = &mapped});
  for (int i = 0; !mapped.done && i < 10000; ++i) {
    wgpuDevicePoll(g->device, true, NULL);
    wgpuInstanceProcessEvents(g->instance);
  }
  if (!mapped.done || !mapped.result)
    return SDL_SetError("WebGPU readback failed: %s", mapped.message);
  const Uint8* rows = wgpuBufferGetConstMappedRange(readback, 0, size);
  Uint32 row = g->target_width * 4;
  Uint8* pixels = rows ? malloc((size_t)row * g->target_height) : NULL;
  bool ok = pixels != NULL;
  if (ok) {
    for (Uint32 y = 0; y < g->target_height; ++y)
      memcpy(pixels + (size_t)y * row, rows + (size_t)y * stride, row);
    ok = otui_save_pixels(path, pixels, (int)g->target_width, (int)g->target_height, false);
  } else {
    SDL_SetError("Could not read the WebGPU capture");
  }
  free(pixels);
  wgpuBufferUnmap(readback);
  return ok;
}

// Offscreen frames and captures draw into the RGBA target, which is copied
// to the window when it has a surface texture; direct frames draw straight
// into the surface texture.
static bool gpu_frame(Gpu* g, const GpuFrame* f, const char* capture, bool* submitted) {
  *submitted = false;
  bool capturing = capture[0] != 0;
  if (capturing &&
      (g->width > INT_MAX / 4 || g->height > INT_MAX || g->width > UINT32_MAX / 4 / g->height))
    return SDL_SetError("Capture exceeds the surface representation");
  if (f->count) wgpuQueueWriteBuffer(g->queue, g->instances, 0, f->rects, f->count * sizeof(Rect));
  if (f->atlas_dirty)
    wgpuQueueWriteTexture(
        g->queue, &(WGPUTexelCopyTextureInfo){.texture = g->atlas, .aspect = WGPUTextureAspect_All},
        f->atlas, ATLAS_BYTES,
        &(WGPUTexelCopyBufferLayout){.bytesPerRow = ATLAS_SIZE, .rowsPerImage = ATLAS_SIZE},
        &(WGPUExtent3D){ATLAS_SIZE, ATLAS_SIZE, 1});
  float viewport[4] = {f->logical_width, f->logical_height, 0, 0};
  wgpuQueueWriteBuffer(g->queue, g->uniforms, 0, viewport, sizeof(viewport));
  if (!check(g)) return false;

  bool indirect = g->offscreen || capturing;
  if (indirect && !ensure_target(g)) return false;
  WGPUTexture surface_texture;
  if (!acquire(g, &surface_texture)) return false;
  WGPUTextureView surface_view =
      surface_texture ? wgpuTextureCreateView(surface_texture, NULL) : NULL;

  WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(g->device, NULL);
  if (indirect) draw(g, f, encoder, g->target_view, g->rect_offscreen);
  else if (surface_view)
    draw(g, f, encoder, surface_view, g->rect_surface ? g->rect_surface : g->rect_offscreen);
  WGPUBuffer readback = NULL;
  Uint32 stride = (g->target_width * 4 + 255) & ~255u;
  if (capturing) {
    readback = wgpuDeviceCreateBuffer(
        g->device,
        &(WGPUBufferDescriptor){.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst,
                                .size = (uint64_t)stride * g->target_height});
    wgpuCommandEncoderCopyTextureToBuffer(
        encoder, &(WGPUTexelCopyTextureInfo){.texture = g->target, .aspect = WGPUTextureAspect_All},
        &(WGPUTexelCopyBufferInfo){
            .buffer = readback,
            .layout = {.bytesPerRow = stride, .rowsPerImage = g->target_height}},
        &(WGPUExtent3D){g->target_width, g->target_height, 1});
  }
  bool ok = true;
  if (indirect && surface_view) ok = copy_to(g, encoder, surface_view);
  WGPUCommandBuffer commands = wgpuCommandEncoderFinish(encoder, NULL);
  wgpuCommandEncoderRelease(encoder);
  wgpuQueueSubmit(g->queue, 1, &commands);
  wgpuCommandBufferRelease(commands);
  *submitted = true;
  if (surface_view) wgpuTextureViewRelease(surface_view);
  if (surface_texture) {
    if (ok) wgpuSurfacePresent(g->surface);
    wgpuTextureRelease(surface_texture);
  }
  ok = check(g) && ok;
  if (readback) {
    if (ok) ok = save(g, readback, stride, capture);
    wgpuBufferRelease(readback);
  }
  return ok;
}

const GpuBackend gpu_webgpu = {gpu_new,     gpu_free,  gpu_driver, gpu_resize,
                               gpu_reserve, gpu_frame, gpu_memory};
