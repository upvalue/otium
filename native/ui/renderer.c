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
#include <plutosvg.h>
#define ATLAS_SIZE 2048
// Glyphs and SVGs are tinted masks, so the atlas holds coverage alone.
#define ATLAS_BYTES (ATLAS_SIZE * ATLAS_SIZE)

// A glyph from a font, or with no font a box-drawing sprite: index is its
// codepoint and w, h the cell it fills.
typedef struct {
  TTF_Font* font;
  Uint32 index;
  int x, y, w, h;
} Glyph;

#define GLYPH_SLOTS 16384

typedef struct Image {
  struct Image* next;
  char* path;
  float width, height, scale;
  int x, y, w, h;
} Image;

typedef struct {
  float rect[4], color[4], shape[4], clip[4], uv[4];
} Rect;

// Recorded draw instances for one widget, replayed at a new position. Origin
// is the widget's position when recorded; scale must match on replay.
typedef struct {
  int id;
  Uint32 used, count;
  float x, y, scale;
  Rect* rects;
} Cache;

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
  Image* images;
  int image_count;
  Glyph glyphs[8192];
  int glyph_slots[GLYPH_SLOTS];  // Glyph index + 1, by hash; 0 is empty.
  int glyph_count, atlas_x, atlas_y, atlas_row;
  bool atlas_dirty;
  SDL_GPUBuffer* buffer;
  SDL_GPUTransferBuffer* upload;
  Rect* rects;
  Uint32 count, capacity, width, height;
  Cache* caches;
  int cache_count, cache_capacity;
  Uint32 frames;
  float logical_width, logical_height, scale;
  float event_x, event_y;
  int event_width, event_height;
  char event_key[128];
  int event_code;
  char* event_text;
  char* clipboard;
  float event_dx, event_dy;
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
      r->event_dx =
          event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -event.wheel.x : event.wheel.x;
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
    if (event.type == SDL_EVENT_WINDOW_MOUSE_LEAVE && event.window.windowID == window_id)
      return "pointer-leave";
    if (event.type == SDL_EVENT_QUIT) return "quit";
    if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == window_id)
      return "quit";
    if (event.type == SDL_EVENT_WINDOW_EXPOSED || event.type == SDL_EVENT_WINDOW_SHOWN ||
        event.type == SDL_EVENT_WINDOW_RESTORED || event.type == SDL_EVENT_WINDOW_FOCUS_GAINED)
      return "expose";
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

float otui_event_dx(Renderer* r) { return r->event_dx; }

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
  while (r->images) {
    Image* next = r->images->next;
    SDL_free(r->images->path);
    free(r->images);
    r->images = next;
  }
  free(r->atlas_pixels);
  free(r->rects);
  for (int i = 0; i < r->cache_count; ++i) free(r->caches[i].rects);
  free(r->caches);
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
      .format = SDL_GPU_TEXTUREFORMAT_R8_UNORM,
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
                                                  .size = ATLAS_BYTES};
  r->atlas_upload = SDL_CreateGPUTransferBuffer(r->device, &atlas_upload);
  r->atlas_pixels = calloc(ATLAS_BYTES, 1);
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

// Caches not replayed for this many frames are freed, so widgets that are
// removed or stay off screen need no explicit release.
#define CACHE_FRAMES 120

// Start a new CPU draw list without discarding its allocated capacity.
void otui_gpu_clear(Renderer* r, Uint32 color) {
  r->count = 0;
  r->clear = rgba(color);
  ++r->frames;
  for (int i = 0; i < r->cache_count;) {
    if (r->frames - r->caches[i].used > CACHE_FRAMES) {
      free(r->caches[i].rects);
      r->caches[i] = r->caches[--r->cache_count];
    } else {
      ++i;
    }
  }
}

// Ensure room for need instances in both the CPU list and the GPU buffers.
static bool reserve(Renderer* r, Uint32 need) {
  if (need <= r->capacity) return true;
  if (need > 65536) return SDL_SetError("UI primitive limit reached");
  Uint32 capacity = r->capacity ? r->capacity : 256;
  while (capacity < need) capacity *= 2;
  Rect* rects = realloc(r->rects, capacity * sizeof(Rect));
  if (!rects) return SDL_OutOfMemory();
  r->rects = rects;
  SDL_GPUBufferCreateInfo b = {.usage = SDL_GPU_BUFFERUSAGE_VERTEX,
                               .size = capacity * sizeof(Rect)};
  SDL_GPUTransferBufferCreateInfo t = {.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD, .size = b.size};
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
  return true;
}

int otui_gpu_mark(Renderer* r) { return (int)r->count; }

static Cache* find_cache(Renderer* r, int id) {
  for (int i = 0; i < r->cache_count; ++i)
    if (r->caches[i].id == id) return &r->caches[i];
  return NULL;
}

// Move the instances recorded since start into cache id, replacing any older
// recording. They leave this frame's list; draw the cache to show them.
bool otui_gpu_cache_store(Renderer* r, int id, int start, float x, float y) {
  if (start < 0 || (Uint32)start > r->count || !isfinite(x) || !isfinite(y))
    return SDL_SetError("Invalid UI cache range");
  Uint32 count = r->count - (Uint32)start;
  Rect* rects = malloc(count ? count * sizeof(Rect) : 1);
  if (!rects) return SDL_OutOfMemory();
  memcpy(rects, r->rects + start, count * sizeof(Rect));
  r->count = (Uint32)start;
  Cache* cache = find_cache(r, id);
  if (!cache) {
    if (r->cache_count == r->cache_capacity) {
      int capacity = r->cache_capacity ? r->cache_capacity * 2 : 16;
      Cache* caches = realloc(r->caches, capacity * sizeof(Cache));
      if (!caches) {
        free(rects);
        return SDL_OutOfMemory();
      }
      r->caches = caches;
      r->cache_capacity = capacity;
    }
    cache = &r->caches[r->cache_count++];
    cache->rects = NULL;
  }
  free(cache->rects);
  *cache = (Cache){id, r->frames, count, x, y, r->scale, rects};
  return true;
}

// Append cache id moved to (x, y), snapped to physical pixels so glyphs stay
// sharp, and clipped to the given rectangle. Instances wholly outside the clip
// are skipped. Returns 1 when drawn, 0 when there is no usable recording and
// -1 on error.
int otui_gpu_cache_draw(Renderer* r, int id, float x, float y, float cx, float cy, float cw,
                        float ch) {
  Cache* cache = find_cache(r, id);
  if (!cache || cache->scale != r->scale) return 0;
  if (!isfinite(x) || !isfinite(y) || !isfinite(cx) || !isfinite(cy) || !isfinite(cw) ||
      !isfinite(ch) || cw < 0 || ch < 0) {
    SDL_SetError("Invalid UI cache position");
    return -1;
  }
  if (!reserve(r, r->count + cache->count)) return -1;
  cache->used = r->frames;
  float dx = roundf((x - cache->x) * r->scale) / r->scale;
  float dy = roundf((y - cache->y) * r->scale) / r->scale;
  float x1 = cx + cw, y1 = cy + ch;
  for (Uint32 i = 0; i < cache->count; ++i) {
    Rect rect = cache->rects[i];
    rect.rect[0] += dx;
    rect.rect[1] += dy;
    rect.clip[0] = fmaxf(rect.clip[0] + dx, cx);
    rect.clip[1] = fmaxf(rect.clip[1] + dy, cy);
    rect.clip[2] = fminf(rect.clip[2] + dx, x1);
    rect.clip[3] = fminf(rect.clip[3] + dy, y1);
    if (rect.clip[0] >= rect.clip[2] || rect.clip[1] >= rect.clip[3] ||
        rect.rect[0] >= rect.clip[2] || rect.rect[1] >= rect.clip[3] ||
        rect.rect[0] + rect.rect[2] <= rect.clip[0] || rect.rect[1] + rect.rect[3] <= rect.clip[1])
      continue;
    r->rects[r->count++] = rect;
  }
  return 1;
}

int otui_gpu_cache_count(Renderer* r) { return r->cache_count; }

// Bytes this renderer holds, by kind, for memory reports. The atlas is kept
// three times: CPU pixels, GPU texture and upload buffer. The instance list is
// too. Swapchain images belong to SDL; kind 5 is the size of one.
long long otui_gpu_memory(Renderer* r, int kind) {
  long long atlas = ATLAS_BYTES;
  long long image = (long long)r->width * r->height * 4;
  switch (kind) {
    case 0: return atlas * 3;
    case 1: return (long long)r->capacity * sizeof(Rect) * 3;
    case 2: {
      long long bytes = (long long)r->cache_capacity * sizeof(Cache);
      for (int i = 0; i < r->cache_count; ++i)
        bytes += (long long)r->caches[i].count * sizeof(Rect);
      return bytes;
    }
    case 3: return r->offscreen ? image : 0;
    case 4: return (long long)sizeof(Renderer);
    case 5: return image;
    default: return -1;
  }
}

// Append one clipped rectangle instance. Grow CPU/GPU storage together;
// no drawing is submitted until otui_gpu_frame. Text uses this path too.
bool otui_gpu_rect(Renderer* r, float x, float y, float w, float h, float radius, Uint32 color,
                   float cx, float cy, float cw, float ch) {
  if (!isfinite(x) || !isfinite(y) || !isfinite(w) || !isfinite(h) || !isfinite(radius) ||
      !isfinite(cx) || !isfinite(cy) || !isfinite(cw) || !isfinite(ch) || w < 0 || h < 0 ||
      radius < 0 || cw < 0 || ch < 0)
    return SDL_SetError("Invalid UI rectangle");
  if (!reserve(r, r->count + 1)) return false;
  SDL_FColor c = rgba(color);
  r->rects[r->count++] = (Rect){{x, y, w, h},
                                {c.r, c.g, c.b, c.a},
                                {w, h, radius, 0},
                                {cx, cy, cx + cw, cy + ch},
                                {0, 0, 0, 0}};
  return true;
}

// Reserve a shelf slot shared by glyphs and SVGs. Never move entries: paint
// recordings retain their UVs until the renderer is destroyed.
static bool atlas_slot(Renderer* r, int w, int h, int* x, int* y) {
  int ax = r->atlas_x, ay = r->atlas_y, row = r->atlas_row;
  if (w + 2 > ATLAS_SIZE || h + 2 > ATLAS_SIZE) return SDL_SetError("UI atlas image is too large");
  if (ax + w + 2 > ATLAS_SIZE) {
    ay += row;
    ax = row = 0;
  }
  if (ay + h + 2 > ATLAS_SIZE) return SDL_SetError("UI glyph/image atlas is full");
  *x = ax + 1;
  *y = ay + 1;
  r->atlas_x = ax + w + 2;
  r->atlas_y = ay;
  r->atlas_row = h + 2 > row ? h + 2 : row;
  r->atlas_dirty = true;
  return true;
}

// SVGs are monochrome masks, tinted by the same shader as glyphs. Colour and
// position do not affect this cache; size and display scale do.
static Image* svg_image(Renderer* r, const char* path, float width, float height) {
  for (Image* image = r->images; image; image = image->next)
    if (image->width == width && image->height == height && image->scale == r->scale &&
        strcmp(image->path, path) == 0)
      return image;
  float pw = ceilf(width * r->scale), ph = ceilf(height * r->scale);
  if (!isfinite(pw) || !isfinite(ph) || pw < 1 || ph < 1 || pw > ATLAS_SIZE - 2 ||
      ph > ATLAS_SIZE - 2) {
    SDL_SetError("Invalid SVG raster size: %s", path);
    return NULL;
  }
  plutosvg_document_t* document = plutosvg_document_load_from_file(path, pw, ph);
  if (!document) {
    SDL_SetError("Cannot load SVG: %s", path);
    return NULL;
  }
  plutovg_surface_t* surface =
      plutosvg_document_render_to_surface(document, NULL, (int)pw, (int)ph, NULL, NULL, NULL);
  plutosvg_document_destroy(document);
  if (!surface) {
    SDL_SetError("Cannot rasterise SVG: %s", path);
    return NULL;
  }
  Image* image = calloc(1, sizeof *image);
  if (!image || !(image->path = SDL_strdup(path))) {
    free(image);
    plutovg_surface_destroy(surface);
    SDL_SetError("Out of memory caching SVG");
    return NULL;
  }
  image->width = width;
  image->height = height;
  image->scale = r->scale;
  image->w = (int)pw;
  image->h = (int)ph;
  if (!atlas_slot(r, image->w, image->h, &image->x, &image->y)) {
    SDL_free(image->path);
    free(image);
    plutovg_surface_destroy(surface);
    return NULL;
  }
  const Uint8* data = plutovg_surface_get_data(surface);
  int stride = plutovg_surface_get_stride(surface);
  for (int y = 0; y < image->h; ++y) {
    const Uint32* src = (const Uint32*)(data + y * stride);
    Uint8* dst = r->atlas_pixels + (image->y + y) * ATLAS_SIZE + image->x;
    for (int x = 0; x < image->w; ++x) dst[x] = src[x] >> 24;
  }
  plutovg_surface_destroy(surface);
  image->next = r->images;
  r->images = image;
  ++r->image_count;
  return image;
}

int otui_gpu_image_count(Renderer* r) { return r->image_count; }

bool otui_gpu_image(Renderer* r, const char* path, float x, float y, float w, float h, Uint32 color,
                    float cx, float cy, float cw, float ch) {
  // Validate the whole command before touching the atlas.
  if (!isfinite(w) || !isfinite(h) || w <= 0 || h <= 0)
    return SDL_SetError("Invalid SVG dimensions");
  Uint32 start = r->count;
  if (!otui_gpu_rect(r, x, y, w, h, 0, color, cx, cy, cw, ch)) return false;
  Image* image = svg_image(r, path, w, h);
  if (!image) {
    r->count = start;
    return false;
  }
  Rect* rect = &r->rects[start];
  rect->rect[0] = roundf(x * r->scale) / r->scale;
  rect->rect[1] = roundf(y * r->scale) / r->scale;
  rect->shape[3] = 1;
  rect->uv[0] = (float)image->x / ATLAS_SIZE;
  rect->uv[1] = (float)image->y / ATLAS_SIZE;
  rect->uv[2] = (float)image->w / ATLAS_SIZE;
  rect->uv[3] = (float)image->h / ATLAS_SIZE;
  return true;
}

static Uint32 glyph_hash(TTF_Font* font, Uint32 index, int w, int h) {
  uintptr_t f = (uintptr_t)font;
  Uint32 x = (Uint32)(f ^ (f >> 32)) * 2654435761u;
  x ^= index * 2246822519u;
  x ^= (Uint32)(w << 16 | h) * 3266489917u;
  return x ^ (x >> 15);
}

static Glyph* find_glyph(Renderer* r, TTF_Font* font, Uint32 index, int w, int h, Uint32* slot) {
  for (Uint32 i = glyph_hash(font, index, w, h);; ++i) {
    *slot = i % GLYPH_SLOTS;
    int at = r->glyph_slots[*slot];
    if (!at) return NULL;
    Glyph* g = &r->glyphs[at - 1];
    if (g->font == font && g->index == index && g->w == w && g->h == h) return g;
  }
}

// Reserve an atlas slot and a glyph entry for a w by h coverage mask.
static Glyph* add_glyph(Renderer* r, TTF_Font* font, Uint32 index, int w, int h, int pw, int ph,
                        Uint32 slot) {
  if (r->glyph_count == 8192) {
    SDL_SetError("UI glyph cache is full");
    return NULL;
  }
  int x, y;
  if (!atlas_slot(r, pw, ph, &x, &y)) return NULL;
  Glyph* g = &r->glyphs[r->glyph_count++];
  *g = (Glyph){font, index, x, y, w, h};
  r->glyph_slots[slot] = r->glyph_count;
  return g;
}

// Cache a rasterized glyph in the atlas, with a one-pixel transparent gutter.
// Font objects outlive atlas entries; only new glyphs mark the upload dirty.
static Glyph* font_glyph(Renderer* r, TTF_Font* font, Uint32 index) {
  Uint32 slot;
  Glyph* g = find_glyph(r, font, index, 0, 0, &slot);
  if (g) return g;
  SDL_Surface* image = TTF_GetGlyphImageForIndex(font, index, NULL);
  if (!image) return NULL;
  SDL_Surface* pixels = SDL_ConvertSurface(image, SDL_PIXELFORMAT_RGBA32);
  SDL_DestroySurface(image);
  if (!pixels) return NULL;
  g = add_glyph(r, font, index, 0, 0, pixels->w, pixels->h, slot);
  if (!g) {
    SDL_DestroySurface(pixels);
    return NULL;
  }
  // RGBA32 is byte-ordered, so alpha is every fourth byte.
  for (int y = 0; y < pixels->h; ++y) {
    const Uint8* src = (const Uint8*)pixels->pixels + y * pixels->pitch;
    Uint8* dst = r->atlas_pixels + (g->y + y) * ATLAS_SIZE + g->x;
    for (int x = 0; x < pixels->w; ++x) dst[x] = src[x * 4 + 3];
  }
  SDL_DestroySurface(pixels);
  return g;
}

static Glyph* glyph(Renderer* r, TTF_CopyOperation* op) {
  return font_glyph(r, op->glyph_font, op->glyph_index);
}

#include "sprites.inc"

// Box-drawing, block, braille and powerline characters fill their cell
// exactly, so they're drawn here rather than taken from the font: lines then
// meet across cells whatever the font's metrics.
static Glyph* sprite(Renderer* r, Uint32 cp, int w, int h) {
  if (!sprite_supported(cp) || w < 1 || h < 1 || w > 512 || h > 512) return NULL;
  Uint32 slot;
  Glyph* g = find_glyph(r, NULL, cp, w, h, &slot);
  if (g) return g;
  Uint8* mask = calloc((size_t)w * h, 1);
  if (!mask) {
    SDL_SetError("Out of memory drawing a cell sprite");
    return NULL;
  }
  sprite_draw(cp, w, h, mask);
  g = add_glyph(r, NULL, cp, w, h, w, h, slot);
  if (g)
    for (int y = 0; y < h; ++y) memcpy(r->atlas_pixels + (g->y + y) * ATLAS_SIZE + g->x, mask + y * w, w);
  free(mask);
  return g;
}

static bool textured_rect(Renderer* r, float x, float y, float w, float h, Uint32 color, Glyph* g,
                          SDL_Rect src, float cx, float cy, float cw, float ch) {
  if (!otui_gpu_rect(r, x, y, w, h, 0, color, cx, cy, cw, ch)) return false;
  Rect* rect = &r->rects[r->count - 1];
  rect->shape[3] = 1;
  rect->uv[0] = (float)(g->x + src.x) / ATLAS_SIZE;
  rect->uv[1] = (float)(g->y + src.y) / ATLAS_SIZE;
  rect->uv[2] = (float)src.w / ATLAS_SIZE;
  rect->uv[3] = (float)src.h / ATLAS_SIZE;
  return true;
}

static Uint32 first_codepoint(const char* s, size_t len, size_t* used) {
  const Uint8* p = (const Uint8*)s;
  Uint32 cp;
  size_t n;
  if (p[0] < 0x80) cp = p[0], n = 1;
  else if ((p[0] & 0xe0) == 0xc0) cp = p[0] & 0x1f, n = 2;
  else if ((p[0] & 0xf0) == 0xe0) cp = p[0] & 0x0f, n = 3;
  else cp = p[0] & 0x07, n = 4;
  if (n > len) n = len;
  for (size_t i = 1; i < n; ++i) cp = cp << 6 | (p[i] & 0x3f);
  *used = n;
  return cp;
}

// Draw terminal cells, separated by 0x1F in text, from column 0 at x, y.
// Each grapheme is shaped alone and placed at its column; cells are cell_w by
// cell_h physical pixels. Flags: 1 underline, 2 strikethrough. On failure,
// nothing from this call is drawn.
bool otui_gpu_cells(Renderer* r, const char* text, int face, float size, float x, float y,
                    int cell_w, int cell_h, Uint32 color, int flags, float cx, float cy, float ccw,
                    float cch) {
  float scale = otui_gpu_scale(r);
  float m[4];
  if (!text || !isfinite(x) || !isfinite(y) || cell_w < 1 || cell_h < 1 || cell_w > 512 ||
      cell_h > 512)
    return SDL_SetError("Invalid terminal cells");
  if (!otui_font_metrics(face, size, scale, m)) return false;
  float left = roundf(x * scale), top = roundf(y * scale);
  // Centre the font's line in the cell, so a taller line height spaces rows evenly.
  float pad = floorf((cell_h - m[1]) / 2);
  Uint32 start = r->count;
  int column = 0;
  for (const char* p = text;; ++column) {
    const char* end = strchr(p, 0x1f);
    size_t len = end ? (size_t)(end - p) : strlen(p);
    float cell_x = left + column * cell_w;
    if (len && !(len == 1 && *p == ' ')) {
      size_t used;
      Uint32 cp = first_codepoint(p, len, &used);
      Glyph* g = used == len ? sprite(r, cp, cell_w, cell_h) : NULL;
      if (g) {
        if (!textured_rect(r, cell_x / scale, top / scale, cell_w / scale, cell_h / scale, color, g,
                           (SDL_Rect){0, 0, cell_w, cell_h}, cx, cy, ccw, cch))
          goto failed;
      } else if (used == len && sprite_supported(cp)) {
        goto failed;
      } else {
        const OtuiCellOp* ops;
        int n = otui_text_cell(p, (int)len, face, size, scale, &ops);
        if (n < 0) goto failed;
        for (int i = 0; i < n; ++i) {
          SDL_Rect src = ops[i].src, dst = ops[i].dst;
          g = font_glyph(r, ops[i].font, ops[i].index);
          if (!g || !textured_rect(r, (cell_x + dst.x) / scale, (top + pad + dst.y) / scale,
                                   dst.w / scale, dst.h / scale, color, g, src, cx, cy, ccw, cch))
            goto failed;
        }
      }
    }
    if (!end) break;
    p = end + 1;
  }
  int thickness = cell_h / 16 > 1 ? cell_h / 16 : 1;
  float width = (float)(column + 1) * cell_w / scale;
  float baseline = top + pad + m[2];
  if ((flags & 1) &&
      !otui_gpu_rect(r, left / scale, fminf(baseline + thickness, top + cell_h - thickness) / scale,
                     width, thickness / scale, 0, color, cx, cy, ccw, cch))
    goto failed;
  if ((flags & 2) &&
      !otui_gpu_rect(r, left / scale, (baseline - roundf(m[2] * 0.3f) - thickness / 2) / scale, width,
                     thickness / scale, 0, color, cx, cy, ccw, cch))
    goto failed;
  return true;
failed:
  r->count = start;
  return false;
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
    memcpy(data, r->atlas_pixels, ATLAS_BYTES);
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
