#include <SDL3/SDL.h>
#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
static const char rect_msl[] = {
#embed "shaders/rect.metal" suffix(, 0)
};
static const unsigned char rect_vert[] = {
#embed "rect.vert.spv"
};
static const unsigned char rect_frag[] = {
#embed "rect.frag.spv"
};
#include "text.h"
#define ATLAS_SIZE 2048

typedef struct {
  TTF_Font* font;
  Uint32 index;
  int x, y;
} Glyph;

typedef struct {
  float rect[4], color[4], shape[4], clip[4], uv[4];
} Rect;

typedef struct QueuedEvent {
  SDL_Event event;
  char* text;
  struct QueuedEvent* next;
} QueuedEvent;

typedef struct Renderer {
  struct Renderer* next;
  QueuedEvent *events, *events_tail;
  char* pending_text;
  SDL_Window* window;
  SDL_GPUDevice* device;
  SDL_GPUGraphicsPipeline* pipeline;
  SDL_GPUTexture* target;  // Owned only in offscreen mode; SDL owns the swapchain.
  bool offscreen;
  SDL_GPUTexture* atlas;
  SDL_GPUSampler* sampler;
  SDL_GPUTransferBuffer* atlas_upload;
  Uint8* atlas_pixels;
  Glyph glyphs[8192];
  int glyph_count, atlas_x, atlas_y, atlas_row;
  bool atlas_dirty;
  SDL_GPUBuffer* buffer;
  SDL_GPUTransferBuffer* upload;
  Rect* rects;
  Uint32 count, capacity, width, height;
  float logical_width, logical_height, scale;
  float event_x, event_y;
  int event_width, event_height;
  char event_key[128];
  int event_code;
  char* event_text;
  char* clipboard;
  float event_dy;
  bool text_input;
  bool event_repeat;
  SDL_FColor clear;
  bool claimed;
} Renderer;

static Renderer* renderers;
static int live_renderers;

int otui_live_renderers(void) { return live_renderers; }

static void queue_event(Renderer* r, const SDL_Event* event) {
  QueuedEvent* node = calloc(1, sizeof *node);
  if (!node) return;
  node->event = *event;
  if (event->type == SDL_EVENT_TEXT_INPUT) {
    node->text = SDL_strdup(event->text.text);
    if (!node->text) {
      free(node);
      return;
    }
    node->event.text.text = node->text;
  }
  if (r->events_tail) r->events_tail->next = node;
  else r->events = node;
  r->events_tail = node;
}

static bool renderer_event(Renderer* r, SDL_Event* event) {
  SDL_free(r->pending_text);
  r->pending_text = NULL;
  if (!r->events) {
    SDL_Event incoming;
    while (SDL_PollEvent(&incoming)) {
      SDL_Window* window = SDL_GetWindowFromEvent(&incoming);
      for (Renderer* target = renderers; target; target = target->next)
        if (incoming.type == SDL_EVENT_QUIT || window == target->window)
          queue_event(target, &incoming);
    }
  }
  if (!r->events) return false;
  QueuedEvent* node = r->events;
  r->events = node->next;
  if (!r->events) r->events_tail = NULL;
  *event = node->event;
  r->pending_text = node->text;
  free(node);
  return true;
}

// Drain SDL events for this renderer. Payload accessors remain valid until the
// next poll; the Otium adapter copies them into a managed event table.
const char* otui_poll_event(Renderer* r) {
  SDL_Event event;
  Uint32 window_id = SDL_GetWindowID(r->window);
  while (renderer_event(r, &event)) {
    if (event.type == SDL_EVENT_TEXT_INPUT && event.text.windowID == window_id) {
      SDL_free(r->event_text);
      r->event_text = SDL_strdup(event.text.text);
      return "text-input";
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL && event.wheel.windowID == window_id) {
      r->event_x = event.wheel.mouse_x;
      r->event_y = event.wheel.mouse_y;
      r->event_dy =
          event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.y : event.wheel.y;
      return "wheel";
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION && event.motion.windowID == window_id) {
      r->event_x = event.motion.x;
      r->event_y = event.motion.y;
      return "pointer-move";
    }
    if ((event.type == SDL_EVENT_WINDOW_RESIZED ||
         event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) &&
        event.window.windowID == window_id) {
      if (event.type == SDL_EVENT_WINDOW_RESIZED) {
        r->event_width = event.window.data1;
        r->event_height = event.window.data2;
      } else if (!SDL_GetWindowSize(r->window, &r->event_width, &r->event_height)) {
        continue;
      }
      if (r->event_width > 0 && r->event_height > 0) return "resize";
      continue;
    }
    if (event.type == SDL_EVENT_QUIT) return "quit";
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == window_id)
      return "quit";
    if (event.type == SDL_EVENT_WINDOW_FOCUS_LOST && event.window.windowID == window_id)
      return "focus-lost";
    if ((event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
        event.key.windowID == window_id) {
      SDL_snprintf(r->event_key, sizeof(r->event_key), "%s%s%s%s%s",
                   event.key.mod & SDL_KMOD_CTRL ? "Ctrl+" : "",
                   event.key.mod & SDL_KMOD_ALT ? "Alt+" : "",
                   event.key.mod & SDL_KMOD_GUI ? "Meta+" : "",
                   event.key.mod & SDL_KMOD_SHIFT ? "Shift+" : "", SDL_GetKeyName(event.key.key));
      r->event_repeat = event.key.repeat;
      r->event_code = event.key.scancode;
      return event.type == SDL_EVENT_KEY_DOWN ? "key-down" : "key-up";
    }
    if ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
        event.button.windowID == window_id && event.button.button == SDL_BUTTON_LEFT) {
      r->event_x = event.button.x;
      r->event_y = event.button.y;
      return event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? "pointer-down" : "pointer-up";
    }
  }
  return "";
}

int otui_event_width(Renderer* r) { return r->event_width; }

int otui_event_height(Renderer* r) { return r->event_height; }

float otui_event_x(Renderer* r) { return r->event_x; }

float otui_event_y(Renderer* r) { return r->event_y; }

const char* otui_event_key(Renderer* r) { return r->event_key; }

int otui_event_code(Renderer* r) { return r->event_code; }

int otui_event_repeat(Renderer* r) { return r->event_repeat; }

const char* otui_event_text(Renderer* r) { return r->event_text ? r->event_text : ""; }

float otui_event_dy(Renderer* r) { return r->event_dy; }

// Keep SDL text input active only while a text-accepting widget has focus.
bool otui_text_input(Renderer* r, bool enabled) {
  if (r->text_input == enabled) return true;
  bool ok = enabled ? SDL_StartTextInput(r->window) : SDL_StopTextInput(r->window);
  if (ok) r->text_input = enabled;
  return ok;
}

// Retain SDL-owned clipboard bytes long enough for the FFI to copy the string.
const char* otui_clipboard_get(Renderer* r) {
  SDL_free(r->clipboard);
  r->clipboard = SDL_GetClipboardText();
  return r->clipboard ? r->clipboard : "";
}

bool otui_clipboard_set(const char* text) { return SDL_SetClipboardText(text); }

// Tear down a complete or partially initialized renderer. Wait once here; normal
// frames and resize operations rely on SDL to defer GPU resource destruction.
void otui_gpu_free(Renderer* r) {
  if (!r) return;
  Renderer** link = &renderers;
  while (*link && *link != r) link = &(*link)->next;
  if (*link) *link = r->next;
  while (r->events) {
    QueuedEvent* next = r->events->next;
    SDL_free(r->events->text);
    free(r->events);
    r->events = next;
  }
  SDL_free(r->pending_text);
  if (r->text_input) SDL_StopTextInput(r->window);
  SDL_free(r->event_text);
  SDL_free(r->clipboard);
  if (r->device) {
    SDL_WaitForGPUIdle(r->device);
    if (r->buffer) SDL_ReleaseGPUBuffer(r->device, r->buffer);
    if (r->upload) SDL_ReleaseGPUTransferBuffer(r->device, r->upload);
    if (r->pipeline) SDL_ReleaseGPUGraphicsPipeline(r->device, r->pipeline);
    if (r->atlas) SDL_ReleaseGPUTexture(r->device, r->atlas);
    if (r->sampler) SDL_ReleaseGPUSampler(r->device, r->sampler);
    if (r->atlas_upload) SDL_ReleaseGPUTransferBuffer(r->device, r->atlas_upload);
    if (r->target) SDL_ReleaseGPUTexture(r->device, r->target);
    if (r->claimed) SDL_ReleaseWindowFromGPUDevice(r->device, r->window);
    SDL_DestroyGPUDevice(r->device);
  }
  free(r->atlas_pixels);
  free(r->rects);
  free(r);
  --live_renderers;
  extern int otui_live_nodes(void);
  if (!live_renderers && !otui_live_nodes()) otui_text_clear();
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

// Direct mode only updates logical geometry and text density: SDL resizes the
// swapchain. Offscreen mode replaces its texture before releasing the old one,
// so a failed allocation leaves the previous dimensions and target intact.
bool otui_gpu_resize(Renderer* r, int width, int height, float scale) {
  if (!r->offscreen && scale != 0)
    return SDL_SetError(
        "Direct rendering requires scale 0; use offscreen mode for explicit scaling");
  if (scale == 0) scale = SDL_GetWindowPixelDensity(r->window);
  if (!isfinite(scale) || width <= 0 || height <= 0 || scale < 1 || scale > 4)
    return SDL_SetError("UI dimensions must be positive and scale must be between 1 and 4");
  double physical_width = ceil((double)width * scale);
  double physical_height = ceil((double)height * scale);
  if (physical_width > UINT32_MAX || physical_height > UINT32_MAX)
    return SDL_SetError("UI dimensions exceed SDL's texture dimension representation");
  Uint32 w = (Uint32)physical_width, h = (Uint32)physical_height;
  if (r->offscreen && (w != r->width || h != r->height || !r->target)) {
    SDL_GPUTextureCreateInfo texture = {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = w,
        .height = h,
        .layer_count_or_depth = 1,
        .num_levels = 1,
    };
    SDL_GPUTexture* target = SDL_CreateGPUTexture(r->device, &texture);
    if (!target) return false;
    // SDL defers destruction until submitted frames have finished using it.
    if (r->target) SDL_ReleaseGPUTexture(r->device, r->target);
    r->target = target;
  }
  r->width = w;
  r->height = h;
  r->logical_width = width;
  r->logical_height = height;
  r->scale = scale;
  return true;
}

// Choose the target mode once. The pipeline must match either the window's
// swapchain format or our RGBA offscreen format. The atlas and instance buffers
// are shared by both paths and survive window resizing.
void* otui_gpu_new(SDL_Window* window, int width, int height, float scale, const char* driver,
                   bool offscreen) {
  if (!window) {
    SDL_SetError("UI renderer requires a window");
    return NULL;
  }
  Renderer* r = calloc(1, sizeof(*r));
  if (!r) {
    SDL_OutOfMemory();
    return NULL;
  }
  ++live_renderers;
  r->window = window;
  r->offscreen = offscreen;
  r->device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_MSL | SDL_GPU_SHADERFORMAT_SPIRV, true,
                                  driver[0] ? driver : NULL);
  if (!r->device) goto failed;
  if (!SDL_ClaimWindowForGPUDevice(r->device, window)) goto failed;
  r->claimed = true;
  if (!otui_gpu_resize(r, width, height, scale)) goto failed;
  SDL_GPUShader *vs = shader(r->device, true), *fs = shader(r->device, false);
  if (!vs || !fs) {
    if (vs) SDL_ReleaseGPUShader(r->device, vs);
    if (fs) SDL_ReleaseGPUShader(r->device, fs);
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
                          : SDL_GetGPUSwapchainTextureFormat(r->device, window),
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
  r->pipeline = SDL_CreateGPUGraphicsPipeline(r->device, &pipeline);
  SDL_ReleaseGPUShader(r->device, vs);
  SDL_ReleaseGPUShader(r->device, fs);
  if (!r->pipeline) goto failed;
  SDL_GPUTextureCreateInfo texture = {
      .type = SDL_GPU_TEXTURETYPE_2D,
      .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
      .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
      .width = ATLAS_SIZE,
      .height = ATLAS_SIZE,
      .layer_count_or_depth = 1,
      .num_levels = 1,
  };
  r->atlas = SDL_CreateGPUTexture(r->device, &texture);
  SDL_GPUSamplerCreateInfo sampler = {.min_filter = SDL_GPU_FILTER_LINEAR,
                                      .mag_filter = SDL_GPU_FILTER_LINEAR,
                                      .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE,
                                      .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE};
  r->sampler = SDL_CreateGPUSampler(r->device, &sampler);
  SDL_GPUTransferBufferCreateInfo atlas_upload = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                                  .size = ATLAS_SIZE * ATLAS_SIZE * 4};
  r->atlas_upload = SDL_CreateGPUTransferBuffer(r->device, &atlas_upload);
  r->atlas_pixels = calloc(ATLAS_SIZE * ATLAS_SIZE, 4);
  if (!r->atlas || !r->sampler || !r->atlas_upload || !r->atlas_pixels) goto failed;
  r->atlas_dirty = true;
  r->next = renderers;
  renderers = r;
  return r;
failed: {
  char message[1024];
  SDL_strlcpy(message, SDL_GetError(), sizeof(message));
  otui_gpu_free(r);
  SDL_SetError("%s", message);
  return NULL;
}
}

float otui_gpu_scale(Renderer* r) { return r->scale; }

bool otui_gpu_offscreen(Renderer* r) { return r->offscreen; }

const char* otui_gpu_driver(Renderer* r) { return SDL_GetGPUDeviceDriver(r->device); }

static SDL_FColor rgba(Uint32 c) {
  return (SDL_FColor){(c >> 24) / 255.0f, ((c >> 16) & 255) / 255.0f, ((c >> 8) & 255) / 255.0f,
                      (c & 255) / 255.0f};
}

// Start a new CPU draw list without discarding its allocated capacity.
void otui_gpu_clear(Renderer* r, Uint32 color) {
  r->count = 0;
  r->clear = rgba(color);
}

// Append one clipped rectangle instance. Grow CPU/GPU storage together;
// no drawing is submitted until otui_gpu_frame. Text uses this path too.
bool otui_gpu_rect(Renderer* r, float x, float y, float w, float h, float radius, Uint32 color,
                   float cx, float cy, float cw, float ch) {
  if (!isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) || !isfinite(radius) ||
      !isfinite(cx) || !isfinite(cy) || !isfinite(cw) || !isfinite(ch) || w < 0 || h < 0 ||
      radius < 0 || cw < 0 || ch < 0)
    return SDL_SetError("Invalid UI rectangle");
  if (r->count == r->capacity) {
    if (r->capacity >= 65536) return SDL_SetError("UI primitive limit reached");
    Uint32 capacity = r->capacity ? r->capacity * 2 : 256;
    Rect* rects = realloc(r->rects, capacity * sizeof(Rect));
    if (!rects) return SDL_OutOfMemory();
    r->rects = rects;
    SDL_GPUBufferCreateInfo b = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                                 .size = capacity * sizeof(Rect)};
    SDL_GPUTransferBufferCreateInfo t = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                                         .size = b.size};
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(r->device, &b);
    SDL_GPUTransferBuffer* upload = SDL_CreateGPUTransferBuffer(r->device, &t);
    if (!buffer || !upload) {
      if (buffer) SDL_ReleaseGPUBuffer(r->device, buffer);
      if (upload) SDL_ReleaseGPUTransferBuffer(r->device, upload);
      return false;
    }
    if (r->buffer) SDL_ReleaseGPUBuffer(r->device, r->buffer);
    if (r->upload) SDL_ReleaseGPUTransferBuffer(r->device, r->upload);
    r->buffer = buffer;
    r->upload = upload;
    r->capacity = capacity;
  }
  SDL_FColor c = rgba(color);
  r->rects[r->count++] = (Rect){{x, y, w, h},
                                {c.r, c.g, c.b, c.a},
                                {w, h, radius, 0},
                                {cx, cy, cx + cw, cy + ch},
                                {0, 0, 0, 0}};
  return true;
}

// Cache a rasterized glyph in the atlas, with a one-pixel transparent gutter.
// Font objects outlive atlas entries; only new glyphs mark the upload dirty.
static Glyph* glyph(Renderer* r, TTF_CopyOperation* op) {
  for (int i = 0; i < r->glyph_count; ++i)
    if (r->glyphs[i].font == op->glyph_font && r->glyphs[i].index == op->glyph_index)
      return &r->glyphs[i];
  SDL_Surface* image = TTF_GetGlyphImageForIndex(op->glyph_font, op->glyph_index, NULL);
  if (!image) return NULL;
  SDL_Surface* pixels = SDL_ConvertSurface(image, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(image);
  if (!pixels) return NULL;
  if (r->atlas_x + pixels->w + 2 > ATLAS_SIZE) {
    r->atlas_y += r->atlas_row;
    r->atlas_x = 0;
    r->atlas_row = 0;
  }
  if (r->glyph_count == 8192 || pixels->w + 2 > ATLAS_SIZE ||
      r->atlas_y + pixels->h + 2 > ATLAS_SIZE) {
    SDL_DestroySurface(pixels);
    SDL_SetError("UI glyph atlas is full");
    return NULL;
  }
  Glyph* g = &r->glyphs[r->glyph_count++];
  *g = (Glyph){op->glyph_font, op->glyph_index, r->atlas_x + 1, r->atlas_y + 1};
  for (int y = 0; y < pixels->h; ++y)
    memcpy(r->atlas_pixels + ((g->y + y) * ATLAS_SIZE + g->x) * 4,
           (Uint8*)pixels->pixels + y * pixels->pitch, pixels->w * 4);
  r->atlas_x += pixels->w + 2;
  if (pixels->h + 2 > r->atlas_row) r->atlas_row = pixels->h + 2;
  r->atlas_dirty = true;
  SDL_DestroySurface(pixels);
  return g;
}

// Expand SDL_ttf shaping operations into atlas-backed rectangle instances.
// On failure, roll back this text run so callers never draw a partial string.
bool otui_gpu_text(Renderer* r, const char* string, int face, float size, float width, float x,
                   float y, Uint32 color, float cx, float cy, float cw, float ch) {
  float scale = otui_gpu_scale(r);
  TTF_Text* text = otui_text_get(string, face, size, width, scale);
  x = roundf(x * scale) / scale;
  y = roundf(y * scale) / scale;
  if (!text) return false;
  Uint32 start = r->count;
  for (int i = 0; i < text->internal->num_ops; ++i) {
    TTF_DrawOperation* op = &text->internal->ops[i];
    SDL_Rect dst;
    Glyph* g = NULL;
    if (op->cmd == TTF_DRAW_COMMAND_COPY) {
      g = glyph(r, &op->copy);
      if (!g) goto failed;
      dst = op->copy.dst;
    } else if (op->cmd == TTF_DRAW_COMMAND_FILL) dst = op->fill.rect;
    else continue;
    if (!otui_gpu_rect(r, x + dst.x / scale, y + dst.y / scale, dst.w / scale, dst.h / scale, 0,
                       color, cx, cy, cw, ch))
      goto failed;
    if (g) {
      Rect* rect = &r->rects[r->count - 1];
      rect->shape[3] = 1;
      rect->uv[0] = (float)(g->x + op->copy.src.x) / ATLAS_SIZE;
      rect->uv[1] = (float)(g->y + op->copy.src.y) / ATLAS_SIZE;
      rect->uv[2] = (float)op->copy.src.w / ATLAS_SIZE;
      rect->uv[3] = (float)op->copy.src.h / ATLAS_SIZE;
    }
  }
  return true;
failed:
  r->count = start;
  return false;
}

int otui_gpu_count(Renderer* r) { return (int)r->count; }

// Copy the CPU-side draw list and any new glyphs into this frame's GPU commands.
// Do all fallible mapping before acquiring a swapchain: SDL forbids cancelling
// a command buffer once it owns a presentable image.
static bool upload_frame(Renderer* r, SDL_GPUCommandBuffer* cmd) {
  if (r->count) {
    void* data = SDL_MapGPUTransferBuffer(r->device, r->upload, true);
    if (!data) return false;
    memcpy(data, r->rects, r->count * sizeof(Rect));
    SDL_UnmapGPUTransferBuffer(r->device, r->upload);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTransferBufferLocation src = {.transfer_buffer = r->upload};
    SDL_GPUBufferRegion dst = {.buffer = r->buffer, .size = r->count * sizeof(Rect)};
    SDL_UploadToGPUBuffer(copy, &src, &dst, true);
    SDL_EndGPUCopyPass(copy);
  }
  if (r->atlas_dirty) {
    void* data = SDL_MapGPUTransferBuffer(r->device, r->atlas_upload, true);
    if (!data) return false;
    memcpy(data, r->atlas_pixels, ATLAS_SIZE * ATLAS_SIZE * 4);
    SDL_UnmapGPUTransferBuffer(r->device, r->atlas_upload);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureTransferInfo src = {.transfer_buffer = r->atlas_upload};
    SDL_GPUTextureRegion dst = {.texture = r->atlas, .w = ATLAS_SIZE, .h = ATLAS_SIZE, .d = 1};
    SDL_UploadToGPUTexture(copy, &src, &dst, false);
    SDL_EndGPUCopyPass(copy);
  }
  return true;
}

// Rectangles and glyphs use one instanced draw. Shader coordinates stay logical;
// the render pass maps them to the actual target's physical pixel dimensions.
static void draw_frame(Renderer* r, SDL_GPUCommandBuffer* cmd, SDL_GPUTexture* image) {
  SDL_GPUColorTargetInfo target = {.texture = image,
                                   .clear_color = r->clear,
                                   .load_op = SDL_GPU_LOADOP_CLEAR,
                                   .store_op = SDL_GPU_STOREOP_STORE,
                                   .cycle = r->offscreen};
  SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(cmd, &target, 1, NULL);
  if (r->count) {
    SDL_BindGPUGraphicsPipeline(pass, r->pipeline);
    SDL_GPUTextureSamplerBinding atlas = {.texture = r->atlas, .sampler = r->sampler};
    SDL_BindGPUFragmentSamplers(pass, 0, &atlas, 1);
    SDL_GPUBufferBinding binding = {.buffer = r->buffer};
    SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
    float viewport[4] = {r->logical_width, r->logical_height, 0, 0};
    SDL_PushGPUVertexUniformData(cmd, 0, viewport, sizeof(viewport));
    SDL_DrawGPUPrimitives(pass, 6, r->count, 0, 0);
  }
  SDL_EndGPURenderPass(pass);
}

// Readback waits for this frame to finish before accessing its pixels. The caller
// owns the download buffer; the surface borrows its mapped bytes until saved.
static bool save_capture(Renderer* r, SDL_GPUCommandBuffer* cmd, SDL_GPUTransferBuffer* download,
                         const char* path) {
  SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmd);
  if (!fence) return false;
  r->atlas_dirty = false;
  bool ok = SDL_WaitForGPUFences(r->device, true, &fence, 1);
  SDL_ReleaseGPUFence(r->device, fence);
  if (!ok) return false;
  void* pixels = SDL_MapGPUTransferBuffer(r->device, download, false);
  if (!pixels) return false;
  SDL_Surface* surface = SDL_CreateSurfaceFrom(
      (int)r->width, (int)r->height,
      (!r->offscreen && (SDL_GetGPUSwapchainTextureFormat(r->device, r->window) ==
                             SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM ||
                         SDL_GetGPUSwapchainTextureFormat(r->device, r->window) ==
                             SDL_GPU_TEXTUREFORMAT_B8G8R8A8_UNORM_SRGB))
          ? SDL_PIXELFORMAT_BGRA32
          : SDL_PIXELFORMAT_RGBA32,
      pixels, (int)(r->width * 4));
  const char* suffix = strrchr(path, '.');
  ok = surface && (suffix && SDL_strcasecmp(suffix, ".png") == 0 ? SDL_SavePNG(surface, path)
                                                                 : SDL_SaveBMP(surface, path));
  if (surface) SDL_DestroySurface(surface);
  SDL_UnmapGPUTransferBuffer(r->device, download);
  return ok;
}

// Direct frames draw into SDL's current swapchain with no intermediate image or
// blit. A minimized/hidden window may have no swapchain image; submit uploads and
// skip drawing in that case. Offscreen frames can still draw and capture, then
// blit to the window if it is visible. Capture memory exists only for this call.
bool otui_gpu_frame(Renderer* r, const char* capture) {
  SDL_GPUTransferBuffer* download = NULL;
  SDL_GPUTexture* temporary = NULL;
  if (capture[0]) {
    // SDL transfer sizes are Uint32, and SDL surface pitch/dimensions are int.
    // Check these API representations before multiplication or narrowing.
    if (r->width > INT_MAX / 4 || r->height > INT_MAX || r->width > UINT32_MAX / 4 / r->height)
      return SDL_SetError("Capture exceeds SDL's transfer buffer or surface representation");
    SDL_GPUTransferBufferCreateInfo info = {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD,
        .size = r->width * r->height * 4,
    };
    download = SDL_CreateGPUTransferBuffer(r->device, &info);
    if (!download) return false;
    if (!r->offscreen) {
      SDL_GPUTextureFormat format = SDL_GetGPUSwapchainTextureFormat(r->device, r->window);
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
          .width = r->width,
          .height = r->height,
          .layer_count_or_depth = 1,
          .num_levels = 1,
      };
      temporary = SDL_CreateGPUTexture(r->device, &texture);
      if (!temporary) goto failed;
    }
  }
  SDL_GPUCommandBuffer* cmd = SDL_AcquireGPUCommandBuffer(r->device);
  if (!cmd) goto failed;
  if (!upload_frame(r, cmd)) {
    SDL_CancelGPUCommandBuffer(cmd);
    goto failed;
  }
  SDL_GPUTexture* swapchain = NULL;
  Uint32 width, height;
  if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, r->window, &swapchain, &width, &height)) {
    SDL_CancelGPUCommandBuffer(cmd);
    goto failed;
  }
  SDL_GPUTexture* image = temporary ? temporary : r->offscreen ? r->target : swapchain;
  if (image) draw_frame(r, cmd, image);
  if (download) {
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(cmd);
    SDL_GPUTextureRegion src = {.texture = image, .w = r->width, .h = r->height, .d = 1};
    SDL_GPUTextureTransferInfo dst = {.transfer_buffer = download};
    SDL_DownloadFromGPUTexture(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);
  }
  if ((r->offscreen || temporary) && swapchain) {
    SDL_GPUBlitInfo blit = {
        .source = {.texture = image, .w = r->width, .h = r->height},
        .destination = {.texture = swapchain, .w = width, .h = height},
        .load_op = SDL_GPU_LOADOP_DONT_CARE,
        .filter = SDL_GPU_FILTER_LINEAR,
    };
    SDL_BlitGPUTexture(cmd, &blit);
  }
  if (download) {
    bool ok = save_capture(r, cmd, download, capture);
    SDL_ReleaseGPUTransferBuffer(r->device, download);
    if (temporary) SDL_ReleaseGPUTexture(r->device, temporary);
    return ok;
  }
  bool ok = SDL_SubmitGPUCommandBuffer(cmd);
  if (ok) r->atlas_dirty = false;
  return ok;
failed:
  if (temporary) SDL_ReleaseGPUTexture(r->device, temporary);
  if (download) SDL_ReleaseGPUTransferBuffer(r->device, download);
  return false;
}
