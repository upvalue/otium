#pragma once
/* The UI renderer's backend interface. renderer.c owns everything that does
 * not depend on the graphics API: events, the draw list, caches and the
 * coverage atlas. A backend turns a frame's draw list and atlas into pixels
 * on the window, and optionally into a capture file. Errors are reported with
 * SDL_SetError and a false or NULL result. */
#include <SDL3/SDL.h>

#define ATLAS_SIZE 2048
// Glyphs and SVGs are tinted masks, so the atlas holds coverage alone.
#define ATLAS_BYTES (ATLAS_SIZE * ATLAS_SIZE)

/* One draw instance, in logical pixels: a rounded rectangle when shape.w is
 * 0, or a tinted atlas mask when it is 1. clip is x0, y0, x1, y1. */
typedef struct {
  float rect[4], color[4], shape[4], clip[4], uv[4];
} Rect;

typedef struct {
  const Rect* rects;
  Uint32 count;
  const Uint8* atlas;  // ATLAS_SIZE by ATLAS_SIZE coverage bytes
  bool atlas_dirty;
  SDL_FColor clear;
  float logical_width, logical_height;
} GpuFrame;

typedef struct Gpu Gpu;

/* Create a backend drawing to window, either directly or through an
 * offscreen target. driver names a backend-specific device, or is empty. */
Gpu* gpu_new(SDL_Window* window, const char* driver, bool offscreen);
void gpu_free(Gpu* gpu);
const char* gpu_driver(Gpu* gpu);
/* Set the physical size of the frame. A failure leaves the old size. */
bool gpu_resize(Gpu* gpu, Uint32 width, Uint32 height);
/* Make room for capacity instances. A failure leaves the old capacity. */
bool gpu_reserve(Gpu* gpu, Uint32 capacity);
/* Draw and present a frame, saving it to capture when that is not empty.
 * submitted is set once the atlas has reached the GPU, even if the frame
 * then fails. */
bool gpu_frame(Gpu* gpu, const GpuFrame* frame, const char* capture, bool* submitted);
/* GPU-side bytes, by the kinds of otui_gpu_memory: 0 atlas, 1 instances,
 * 3 offscreen target. Other kinds are 0. */
long long gpu_memory(Gpu* gpu, int kind);

/* Save 8-bit RGBA or BGRA pixels, tightly packed, as PNG when path ends in
 * .png and BMP otherwise. */
bool otui_save_pixels(const char* path, void* pixels, int width, int height, bool bgra);
