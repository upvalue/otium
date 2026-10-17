#include <SDL3/SDL.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "shaders.h"

typedef struct { float rect[4], color[4], shape[4], clip[4]; } Rect;
typedef struct {
  SDL_Window* window;
  SDL_GPUDevice* device;
  SDL_GPUGraphicsPipeline* pipeline;
  SDL_GPUTexture* target;
  SDL_GPUBuffer* buffer;
  SDL_GPUTransferBuffer *upload, *download;
  Rect* rects;
  Uint32 count, capacity, width, height;
  float logical_width, logical_height;
  float event_x, event_y;
  SDL_FColor clear;
  bool claimed;
} Renderer;
static int live_renderers;
int otui_live_renderers(void) { return live_renderers; }

int otui_poll_event(Renderer* r) {
  SDL_Event event;
  Uint32 window_id = SDL_GetWindowID(r->window);
  while (SDL_PollEvent(&event)) {
    if (event.type == SDL_EVENT_QUIT) return 1;
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
        event.window.windowID == window_id) return 1;
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST &&
        event.window.windowID == window_id) return 4;
    if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
         event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        event.button.windowID == window_id && event.button.button == SDL_BUTTON_LEFT) {
      r->event_x = event.button.x;
      r->event_y = event.button.y;
      return event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? 2 : 3;
    }
  }
  return 0;
}
float otui_event_x(Renderer* r) { return r->event_x; }
float otui_event_y(Renderer* r) { return r->event_y; }

void otui_gpu_free(Renderer* r) {
  if (!r) return;
  if (r->device) {
    SDL_WaitForGPUIdle(r->device);
    if (r->buffer) SDL_ReleaseGPUBuffer(r->device, r->buffer);
    if (r->upload) SDL_ReleaseGPUTransferBuffer(r->device, r->upload);
    if (r->download) SDL_ReleaseGPUTransferBuffer(r->device, r->download);
    if (r->pipeline) SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipeline);
    if (r->target) SDL_ReleaseGPUTexture(r->device, r->target);
    if (r->claimed) SDL_ReleaseWindowFromGPUDevice(r->device, r->window);
    SDL_DestroyGPUDevice(r->device);
  }
  free(r->rects);
  free(r);
  --live_renderers;
}
static SDL_GPUShader* shader(SDL_GPUDevice* device, bool vertex) {
  bool metal = (SDL_GetGPUShaderFormats(device) & SDL_GPU_SHADERFORMAT_MSL) != 0;
  SDL_GPUShaderCreateInfo info = {
    .code_size = metal ? strlen(rect_msl) : vertex ? sizeof(rect_vert) : sizeof(rect_frag),
    .code = metal ? (const Uint8*)rect_msl : vertex ? rect_vert : rect_frag,
    .entrypoint = metal ? (vertex ? "vertex_main" : "fragment_main") : "main",
    .format = metal ? SDL_GPU_SHADERFORMAT_MSL : SDL_GPU_SHADERFORMAT_SPIRV,
    .stage = vertex ? SDL_GPU_SHADERSTAGE_VERTEX : SDL_GPU_SHADERSTAGE_FRAGMENT,
    .num_uniform_buffers = vertex ? 1 : 0,
  };
  return SDL_CreateGPUShader(device, &info);
}
void* otui_gpu_new(SDL_Window* window, int width, int height, int scale, const char* driver) {
  if (!window || width <= 0 || height <= 0 || scale < 1 || scale > 4 ||
      width > 4096 / scale || height > 4096 / scale) {
    SDL_SetError("UI target must be positive and at most 4096 physical pixels per axis");
    return NULL;
  }
  Renderer* r = calloc(1, sizeof(*r));
  if (!r) { SDL_OutOfMemory(); return NULL; }
  ++live_renderers;
  r->window = window; r->width = width * scale; r->height = height * scale;
  r->logical_width = (float)width; r->logical_height = (float)height;
  r->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV,
                                  true, driver[0] ? driver : NULL);
  if (!r->device) goto failed;
  if (!SDL_ClaimWindowForGPUDevice(r->device, window)) goto failed;
  r->claimed = true;
  SDL_GPUTextureCreateInfo texture = {
    .type = SDL_GPU_TEXTURETYPE_2D, .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
    .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
    .width = r->width, .height = r->height, .layer_count_or_depth = 1, .num_levels = 1,
  };
  r->target = SDL_CreateGPUTexture(r->device, &texture);
  if (!r->target) goto failed;
  SDL_GPUTransferBufferCreateInfo download = {
    .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD, .size = r->width * r->height * 4,
  };
  r->download = SDL_CreateGPUTransferBuffer(r->device, &download);
  if (!r->download) goto failed;
  SDL_GPUShader *vs = shader(r->device, true), *fs = shader(r->device, false);
  if (!vs || !fs) {
    if (vs) SDL_ReleaseGPUShader(r->device, vs);
    if (fs) SDL_ReleaseGPUShader(r->device, fs);
    SDL_SetError("Could not compile UI shaders; see the SDL log");
    goto failed;
  }
  SDL_GPUVertexBufferDescription vb = {
    .slot = 0, .pitch = sizeof(Rect), .input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE,
  };
  SDL_GPUVertexAttribute attrs[4];
  for (Uint32 i = 0; i < 4; ++i)
    attrs[i] = (SDL_GPUVertexAttribute){.location=i, .buffer_slot=0,
                                       .format=SDL_GPU_VERTEXELEMENTFORMAT_FLOAT4, .offset=i*16};
  SDL_GPUColorTargetDescription color = {
    .format = texture.format,
    .blend_state = {.enable_blend = true, .src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
      .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      .color_blend_op = SDL_GPU_BLENDOP_ADD, .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
      .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
      .alpha_blend_op = SDL_GPU_BLENDOP_ADD},
  };
  SDL_GPUGraphicsPipelineCreateInfo pipeline = {
    .vertex_shader = vs, .fragment_shader = fs,
    .vertex_input_state = {.vertex_buffer_descriptions=&vb, .num_vertex_buffers=1,
                          .vertex_attributes=attrs, .num_vertex_attributes=4},
    .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
    .target_info = {.color_target_descriptions=&color, .num_color_targets=1},
  };
  r->pipeline = SDL_CreateGPUGraphicsPipeline(r->device, &pipeline);
  SDL_ReleaseGPUShader(r->device, vs); SDL_ReleaseGPUShader(r->device, fs);
  if (!r->pipeline) goto failed;
  return r;
failed: {
    char message[1024]; SDL_strlcpy(message, SDL_GetError(), sizeof(message));
    otui_gpu_free(r); SDL_SetError("%s", message); return NULL;
  }
}
const char* otui_gpu_driver(Renderer* r) { return SDL_GetGPUDeviceDriver(r->device); }
static SDL_FColor rgba(Uint32 c) {
  return (SDL_FColor){(c >> 24)/255.0f, ((c >> 16)&255)/255.0f,
                      ((c >> 8)&255)/255.0f, (c&255)/255.0f};
}
void otui_gpu_clear(Renderer* r, Uint32 color) { r->count = 0; r->clear = rgba(color); }
bool otui_gpu_rect(Renderer* r, float x, float y, float w, float h, float radius,
                    Uint32 color, float cx, float cy, float cw, float ch) {
  if (!isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) || !isfinite(radius) ||
      !isfinite(cx) || !isfinite(cy) || !isfinite(cw) || !isfinite(ch) ||
      w < 0 || h < 0 || radius < 0 || cw < 0 || ch < 0)
    return SDL_SetError("Invalid UI rectangle");
  if (r->count == r->capacity) {
    if (r->capacity >= 65536) return SDL_SetError("UI primitive limit reached");
    Uint32 capacity = r->capacity ? r->capacity * 2 : 256;
    Rect* rects = realloc(r->rects, capacity * sizeof(Rect));
    if (!rects) return SDL_OutOfMemory();
    r->rects = rects;
    SDL_GPUBufferCreateInfo b = {.usage=SDL_GPU_BUFFERUSAGE_VERTEX, .size=capacity*sizeof(Rect)};
    SDL_GPUTransferBufferCreateInfo t = {.usage=SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size=b.size};
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(r->device, &b);
    SDL_GPUTransferBuffer* upload = SDL_CreateGPUTransferBuffer(r->device, &t);
    if (!buffer || !upload) {
      if (buffer) SDL_ReleaseGPUBuffer(r->device, buffer);
      if (upload) SDL_ReleaseGPUTransferBuffer(r->device, upload);
      return false;
    }
    if (r->buffer) SDL_ReleaseGPUBuffer(r->device, r->buffer);
    if (r->upload) SDL_ReleaseGPUTransferBuffer(r->device, r->upload);
    r->buffer = buffer; r->upload = upload; r->capacity = capacity;
  }
  SDL_FColor c = rgba(color);
  r->rects[r->count++] = (Rect){{x,y,w,h},{c.r,c.g,c.b,c.a},{w,h,radius,0},{cx,cy,cx+cw,cy+ch}};
  return true;
}
int otui_gpu_count(Renderer* r) { return (int)r->count; }
bool otui_gpu_frame(Renderer* r, const char* capture) {
  if (r->count) {
    void* data = SDL_MapGPUTransferBuffer(r->device, r->upload, true);
    if (!data) return false;
    memcpy(data, r->rects, r->count * sizeof(Rect));
    SDL_UnmapGPUTransferBuffer(r->device, r->upload);
  }
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r->device);
  if (!cmd) return false;
  if (r->count) {
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {.transfer_buffer=r->upload};
    SDL_GPUBufferRegion dst = {.buffer=r->buffer, .size=r->count*sizeof(Rect)};
    SDL_UploadToGPUBuffer(copy, &src, &dst, true); SDL_EndGPUCopyPass(copy);
  }
  SDL_GPUColorTargetInfo target = {.texture=r->target, .clear_color=r->clear,
    .load_op=SDL_GPU_LOADOP_CLEAR, .store_op=SDL_GPU_STOREOP_STORE, .cycle=true};
  SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
  if (r->count) {
    SDL_BindGPUGraphicsPipeline(pass, r->pipeline);
    SDL_GPUBufferBinding binding = {.buffer=r->buffer};
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
    float viewport[4] = {r->logical_width, r->logical_height, 0, 0};
    SDL_PushGPUVertexUniformData(cmd, 0, viewport, sizeof(viewport));
    SDL_DrawGPUPrimitives(pass, 6, r->count, 0, 0);
  }
  SDL_EndGPURenderPass(pass);
  if (capture[0]) {
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src = {.texture=r->target, .w=r->width, .h=r->height, .d=1};
    SDL_GPUTextureTransferInfo dst = {.transfer_buffer=r->download};
    SDL_DownloadFromGPUTexture(copy, &src, &dst); SDL_EndGPUCopyPass(copy);
  }
  SDL_GPUTexture* swapchain = NULL;
  Uint32 width, height;
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, r->window, &swapchain, &width, &height)) {
    SDL_CancelGPUCommandBuffer(cmd); return false;
  }
  if (swapchain) {
    SDL_GPUBlitInfo blit = {
      .source={.texture=r->target, .w=r->width, .h=r->height},
      .destination={.texture=swapchain, .w=width, .h=height},
      .load_op=SDL_GPU_LOADOP_DONT_CARE, .filter=SDL_GPU_FILTER_LINEAR,
    };
    SDL_BlitGPUTexture(cmd, &blit);
  }
  if (!capture[0]) return SDL_SubmitGPUCommandBuffer(cmd);
  SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (!fence) return false;
  bool ok = SDL_WaitForGPUFences(r->device, true, &fence, 1);
  SDL_ReleaseGPUFence(r->device, fence);
  if (!ok) return false;
  void* pixels = SDL_MapGPUTransferBuffer(r->device, r->download, false);
  if (!pixels) return false;
  SDL_Surface* surface = SDL_CreateSurfaceFrom(r->width, r->height, SDL_PIXELFORMAT_RGBA32,
                                              pixels, r->width*4);
  const char* suffix = strrchr(capture, '.');
  ok = surface && (suffix && SDL_strcasecmp(suffix, ".png") == 0
                       ? SDL_SavePNG(surface, capture) : SDL_SaveBMP(surface, capture));
  if (surface) SDL_DestroySurface(surface);
  SDL_UnmapGPUTransferBuffer(r->device, r->download);
  return ok;
}
