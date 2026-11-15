// The SDL_GPU backend for the UI renderer.
#include <SDL3/SDL.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "renderer.h"
static const char rect_msl[] = {
#embed "shaders/rect.metal" suffix(, 0)
};
static const unsigned char rect_vert[] = {
#embed "rect.vert.spv"
};
static const unsigned char rect_frag[] = {
#embed "rect.frag.spv"
};

struct Gpu {
  SDL_Window* window;
  SDL_GPUDevice* device;
  SDL_GPUGraphicsPipeline* pipeline;
  SDL_GPUTexture* target;  // Owned only in offscreen mode; SDL owns the swapchain.
  bool offscreen, claimed;
  SDL_GPUTexture* atlas;
  SDL_GPUSampler* sampler;
  SDL_GPUTransferBuffer* atlas_upload;
  SDL_GPUBuffer* buffer;
  SDL_GPUTransferBuffer* upload;
  Uint32 capacity, width, height;
};

// Tear down a complete or partially initialized backend. Wait once here; normal
// frames and resize operations rely on SDL to defer GPU resource destruction.
void gpu_free(Gpu* g) {
  if (!g) return;
  if (g->device) {
    SDL_WaitForGPUIdle(g->device);
    if (g->buffer) SDL_ReleaseGPUBuffer(g->device, g->buffer);
    if (g->upload) SDL_ReleaseGPUTransferBuffer(g->device, g->upload);
    if (g->pipeline) SDL_ReleaseGPUGraphicsPipeline(g->device, g->pipeline);
    if (g->atlas) SDL_ReleaseGPUTexture(g->device, g->atlas);
    if (g->sampler) SDL_ReleaseGPUSampler(g->device, g->sampler);
    if (g->atlas_upload) SDL_ReleaseGPUTransferBuffer(g->device, g->atlas_upload);
    if (g->target) SDL_ReleaseGPUTexture(g->device, g->target);
    if (g->claimed) SDL_ReleaseWindowFromGPUDevice(g->device, g->window);
    SDL_DestroyGPUDevice(g->device);
  }
  free(g);
}

// Load the same rectangle/glyph shader from Metal source or embedded SPIR-V.
static SDL_GPUShader* shader(SDL_GPUDevice* device, bool vertex) {
  bool metal = (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_MSL) != 0;
  SDL_GPUShaderCreateInfo info = {
      .code_size = metal    ? strlen(rect_msl)
                   : vertex ? sizeof(rect_vert)
                            : sizeof(rect_frag),
      .code = metal    ? (const Uint8*)rect_msl
              : vertex ? rect_vert
                       : rect_frag,
      .entrypoint = metal ? (vertex ? "vertex_main" : "fragment_main") : "main",
      .format = metal ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_SPIRV,
      .stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT,
      .num_samplers = vertex ? 0 : 1,
      .num_uniform_buffers = vertex ? 1 : 0,
  };
  return SDL_CreateGPUShader(device, &info);
}

// Direct mode only records the size: SDL resizes the swapchain. Offscreen mode
// replaces its texture before releasing the old one, so a failed allocation
// leaves the previous dimensions and target intact.
bool gpu_resize(Gpu* g, Uint32 w, Uint32 h) {
  if (g->offscreen && (w != g->width || h != g->height || !g->target)) {
    SDL_GPUTextureCreateInfo texture = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = w,
        .height = h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };
    SDL_GPUTexture* target = SDL_CreateGPUTexture(g->device, &texture);
    if (!target) return false;
    // SDL defers destruction until submitted frames have finished using it.
    if (g->target) SDL_ReleaseGPUTexture(g->device, g->target);
    g->target = target;
  }
  g->width = w;
  g->height = h;
  return true;
}

// The pipeline must match either the window's swapchain format or our RGBA
// offscreen format. The atlas and instance buffers survive window resizing.
Gpu* gpu_new(SDL_Window* window, const char* driver, bool offscreen) {
  Gpu* g = calloc(1, sizeof(*g));
  if (!g) {
    SDL_OutOfMemory();
    return NULL;
  }
  g->window = window;
  g->offscreen = offscreen;
  g->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV, true,
                                  driver[0] ? driver : NULL);
  if (!g->device) goto failed;
  if (!SDL_ClaimWindowForGPUDevice(g->device, window)) goto failed;
  g->claimed = true;
  SDL_GPUShader *vs = shader(g->device, true), *fs = shader(g->device, false);
  if (!vs || !fs) {
    if (vs) SDL_ReleaseGPUShader(g->device, vs);
    if (fs) SDL_ReleaseGPUShader(g->device, fs);
    SDL_SetError("Could not compile UI shaders; see the SDL log");
    goto failed;
  }
  SDL_GPUVertexBufferDescription vb = {
      .slot = 0,
      .pitch = sizeof(Rect),
      .input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE,
  };
  SDL_GPUVertexAttribute attrs[5];
  for (Uint32 i = 0; i < 5; ++i)
    attrs[i] = (SDL_GPUVertexAttribute){.location = i,
                                        .buffer_slot = 0,
                                        .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4,
                                        .offset = i * 16};
  SDL_GPUColorTargetDescription color = {
      .format = offscreen ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
                          : SDL_GetGPUSwapchainTextureFormat(g->device, window),
      .blend_state = {.enable_blend = true,
                      .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                      .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                      .color_blend_op = SDL_GPU_BLENDOP_ADD,
                      .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
                      .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
                      .alpha_blend_op = SDL_GPU_BLENDOP_ADD},
  };
  SDL_GPUGraphicsPipelineCreateInfo pipeline = {
      .vertex_shader = vs,
      .fragment_shader = fs,
      .vertex_input_state = {.vertex_buffer_descriptions = &vb,
                             .num_vertex_buffers = 1,
                             .vertex_attributes = attrs,
                             .num_vertex_attributes = 5},
      .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
      .target_info = {.color_target_descriptions = &color, .num_color_targets = 1},
  };
  g->pipeline = SDL_CreateGPUGraphicsPipeline(g->device, &pipeline);
  SDL_ReleaseGPUShader(g->device, vs);
  SDL_ReleaseGPUShader(g->device, fs);
  if (!g->pipeline) goto failed;
  SDL_GPUTextureCreateInfo texture = {
      .type = SDL_GPU_TEXTURETYPE_2D,
      .format = SDL_GPU_TEXTUREFORMAT_R8_UNORM,
      .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
      .width = ATLAS_SIZE,
      .height = ATLAS_SIZE,
      .layer_count_or_depth = 1,
      .num_levels = 1,
  };
  g->atlas = SDL_CreateGPUTexture(g->device, &texture);
  SDL_GPUSamplerCreateInfo sampler = {.min_filter = SDL_GPU_FILTER_LINEAR,
                                      .mag_filter = SDL_GPU_FILTER_LINEAR,
                                      .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
                                      .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE};
  g->sampler = SDL_CreateGPUSampler(g->device, &sampler);
  SDL_GPUTransferBufferCreateInfo atlas_upload = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                  .size = ATLAS_BYTES};
  g->atlas_upload = SDL_CreateGPUTransferBuffer(g->device, &atlas_upload);
  if (!g->atlas || !g->sampler || !g->atlas_upload) goto failed;
  return g;
failed:
  gpu_free(g);
  return NULL;
}

const char* gpu_driver(Gpu* g) { return SDL_GetGPUDeviceDriver(g->device); }

bool gpu_reserve(Gpu* g, Uint32 capacity) {
  if (capacity <= g->capacity) return true;
  SDL_GPUBufferCreateInfo b = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                               .size = capacity * sizeof(Rect)};
  SDL_GPUTransferBufferCreateInfo t = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = b.size};
  SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(g->device, &b);
  SDL_GPUTransferBuffer* upload = SDL_CreateGPUTransferBuffer(g->device, &t);
  if (!buffer || !upload) {
    if (buffer) SDL_ReleaseGPUBuffer(g->device, buffer);
    if (upload) SDL_ReleaseGPUTransferBuffer(g->device, upload);
    return false;
  }
  if (g->buffer) SDL_ReleaseGPUBuffer(g->device, g->buffer);
  if (g->upload) SDL_ReleaseGPUTransferBuffer(g->device, g->upload);
  g->buffer = buffer;
  g->upload = upload;
  g->capacity = capacity;
  return true;
}

// The atlas is held twice, as a texture and an upload buffer. So are the
// instances. Swapchain images belong to SDL.
long long gpu_memory(Gpu* g, int kind) {
  switch (kind) {
    case 0: return (long long)ATLAS_BYTES * 2;
    case 1: return (long long)g->capacity * sizeof(Rect) * 2;
    case 3: return g->offscreen ? (long long)g->width * g->height * 4 : 0;
    default: return 0;
  }
}

// Copy the CPU-side draw list and any new glyphs into this frame's GPU commands.
// Do all fallible mapping before acquiring a swapchain: SDL forbids cancelling
// a command buffer once it owns a presentable image.
static bool upload_frame(Gpu* g, const GpuFrame* f, SDL_GPUCommandBuffer* cmd) {
  if (f->count) {
    void* data = SDL_MapGPUTransferBuffer(g->device, g->upload, true);
    if (!data) return false;
    memcpy(data, f->rects, f->count * sizeof(Rect));
    SDL_UnmapGPUTransferBuffer(g->device, g->upload);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {.transfer_buffer = g->upload};
    SDL_GPUBufferRegion dst = {.buffer = g->buffer, .size = f->count * sizeof(Rect)};
    SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    SDL_EndGPUCopyPass(copy);
  }
  if (f->atlas_dirty) {
    void* data = SDL_MapGPUTransferBuffer(g->device, g->atlas_upload, true);
    if (!data) return false;
    memcpy(data, f->atlas, ATLAS_BYTES);
    SDL_UnmapGPUTransferBuffer(g->device, g->atlas_upload);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {.transfer_buffer = g->atlas_upload};
    SDL_GPUTextureRegion dst = {.texture = g->atlas, .w = ATLAS_SIZE, .h = ATLAS_SIZE, .d = 1};
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
  }
  return true;
}

// Rectangles and glyphs use one instanced draw. Shader coordinates stay logical;
// the render pass maps them to the actual target's physical pixel dimensions.
static void draw_frame(Gpu* g, const GpuFrame* f, SDL_GPUCommandBuffer* cmd,
                       SDL_GPUTexture* image) {
  SDL_GPUColorTargetInfo target = {.texture = image,
                                   .clear_color = f->clear,
                                   .load_op = SDL_GPU_LOADOP_CLEAR,
                                   .store_op = SDL_GPU_STOREOP_STORE,
                                   .cycle = g->offscreen};
  SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
  if (f->count) {
    SDL_BindGPUGraphicsPipeline(pass, g->pipeline);
    SDL_GPUTextureSamplerBinding atlas = {.texture = g->atlas, .sampler = g->sampler};
    SDL_BindGPUFragmentSamplers(pass, 0, &atlas, 1);
    SDL_GPUBufferBinding binding = {.buffer = g->buffer};
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
    float viewport[4] = {f->logical_width, f->logical_height, 0, 0};
    SDL_PushGPUVertexUniformData(cmd, 0, viewport, sizeof(viewport));
    SDL_DrawGPUPrimitives(pass, 6, f->count, 0, 0);
  }
  SDL_EndGPURenderPass(pass);
}

// Readback waits for this frame to finish before accessing its pixels. The caller
// owns the download buffer.
static bool save_capture(Gpu* g, SDL_GPUCommandBuffer* cmd, SDL_GPUTransferBuffer* download,
                         const char* path, bool* submitted) {
  SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (!fence) return false;
  *submitted = true;
  bool ok = SDL_WaitForGPUFences(g->device, true, &fence, 1);
  SDL_ReleaseGPUFence(g->device, fence);
  if (!ok) return false;
  void* pixels = SDL_MapGPUTransferBuffer(g->device, download, false);
  if (!pixels) return false;
  SDL_GPUTextureFormat format =
      g->offscreen ? SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM
                   : SDL_GetGPUSwapchainTextureFormat(g->device, g->window);
  ok = otui_save_pixels(path, pixels, (int)g->width, (int)g->height,
                        format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
                            format == SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB);
  SDL_UnmapGPUTransferBuffer(g->device, download);
  return ok;
}

// Direct frames draw into SDL's current swapchain with no intermediate image or
// blit. A minimized/hidden window may have no swapchain image; submit uploads and
// skip drawing in that case. Offscreen frames can still draw and capture, then
// blit to the window if it is visible. Capture memory exists only for this call.
bool gpu_frame(Gpu* g, const GpuFrame* f, const char* capture, bool* submitted) {
  SDL_GPUTransferBuffer* download = NULL;
  SDL_GPUTexture* temporary = NULL;
  *submitted = false;
  if (capture[0]) {
    // SDL transfer sizes are Uint32, and SDL surface pitch/dimensions are int.
    // Check these API representations before multiplication or narrowing.
    if (g->width > INT_MAX / 4 || g->height > INT_MAX || g->width > UINT32_MAX / 4 / g->height)
      return SDL_SetError("Capture exceeds SDL's transfer buffer or surface representation");
    SDL_GPUTransferBufferCreateInfo info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size = g->width * g->height * 4,
    };
    download = SDL_CreateGPUTransferBuffer(g->device, &info);
    if (!download) return false;
    if (!g->offscreen) {
      SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(g->device, g->window);
      if (format != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM &&
          format != SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM_SRGB &&
          format != SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM &&
          format != SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB) {
        SDL_SetError("Capture requires an 8-bit RGBA or BGRA swapchain");
        goto failed;
      }
      SDL_GPUTextureCreateInfo texture = {
          .type = SDL_GPU_TEXTURETYPE_2D,
          .format = format,
          .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
          .width = g->width,
          .height = g->height,
          .layer_count_or_depth = 1,
          .num_levels = 1,
      };
      temporary = SDL_CreateGPUTexture(g->device, &texture);
      if (!temporary) goto failed;
    }
  }
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(g->device);
  if (!cmd) goto failed;
  if (!upload_frame(g, f, cmd)) {
    SDL_CancelGPUCommandBuffer(cmd);
    goto failed;
  }
  SDL_GPUTexture* swapchain = NULL;
  Uint32 width, height;
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, g->window, &swapchain, &width, &height)) {
    SDL_CancelGPUCommandBuffer(cmd);
    goto failed;
  }
  SDL_GPUTexture* image = temporary ? temporary : g->offscreen ? g->target : swapchain;
  if (image) draw_frame(g, f, cmd, image);
  if (download) {
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src = {.texture = image, .w = g->width, .h = g->height, .d = 1};
    SDL_GPUTextureTransferInfo dst = {.transfer_buffer = download};
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
  }
  if ((g->offscreen || temporary) && swapchain) {
    SDL_GPUBlitInfo blit = {
        .source = {.texture = image, .w = g->width, .h = g->height},
        .destination = {.texture = swapchain, .w = width, .h = height},
        .load_op = SDL_GPU_LOADOP_DONT_CARE,
        .filter = SDL_GPU_FILTER_LINEAR,
    };
    SDL_BlitGPUTexture(cmd, &blit);
  }
  if (download) {
    bool ok = save_capture(g, cmd, download, capture, submitted);
    SDL_ReleaseGPUTransferBuffer(g->device, download);
    if (temporary) SDL_ReleaseGPUTexture(g->device, temporary);
    return ok;
  }
  bool ok = SDL_SubmitGPUCommandBuffer(cmd);
  if (ok) *submitted = true;
  return ok;
failed:
  if (temporary) SDL_ReleaseGPUTexture(g->device, temporary);
  if (download) SDL_ReleaseGPUTransferBuffer(g->device, download);
  return false;
}
