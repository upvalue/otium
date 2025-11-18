#ifndef OT_USER_GRAPHICS_BACKEND_SDL_HPP
#define OT_USER_GRAPHICS_BACKEND_SDL_HPP

#include "ot/user/graphics/backend.hpp"
#include "ot/user/user.hpp"

#include <SDL2/SDL.h>

/**
 * SDL graphics backend for WASM and other platforms.
 * Uses SDL2 for cross-platform graphics support with Emscripten.
 */
class SdlGraphicsBackend : public GraphicsBackend {
private:
  SDL_Window *window;
  SDL_Renderer *renderer;
  SDL_Texture *texture;
  uint32_t *framebuffer;
  uint32_t width;
  uint32_t height;

public:
  SdlGraphicsBackend() : window(nullptr), renderer(nullptr), texture(nullptr), framebuffer(nullptr), width(640),
                         height(480) {}

  bool init() override {
    oprintf("SdlGraphicsBackend: Initializing %ux%u framebuffer\n", width, height);

    // Initialize SDL video subsystem
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
      oprintf("SdlGraphicsBackend: SDL_Init failed: %s\n", SDL_GetError());
      return false;
    }

    // Create window
    window = SDL_CreateWindow("Otium OS", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, width, height,
                              SDL_WINDOW_SHOWN);
    if (!window) {
      oprintf("SdlGraphicsBackend: SDL_CreateWindow failed: %s\n", SDL_GetError());
      SDL_Quit();
      return false;
    }

    // Create renderer
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!renderer) {
      oprintf("SdlGraphicsBackend: SDL_CreateRenderer failed: %s\n", SDL_GetError());
      SDL_DestroyWindow(window);
      SDL_Quit();
      return false;
    }

    // Create texture for framebuffer (BGRA format to match VirtIO backend)
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture) {
      oprintf("SdlGraphicsBackend: SDL_CreateTexture failed: %s\n", SDL_GetError());
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      SDL_Quit();
      return false;
    }

    // Allocate framebuffer from user-space page allocator
    uint32_t fb_size = width * height * sizeof(uint32_t);
    uint32_t pages_needed = (fb_size + OT_PAGE_SIZE - 1) / OT_PAGE_SIZE;

    oprintf("SdlGraphicsBackend: Allocating %u pages for framebuffer\n", pages_needed);

    // Allocate enough pages to hold the framebuffer
    PageAddr first_page = PageAddr((uintptr_t)ou_alloc_page());
    if (first_page.raw() == 0) {
      oprintf("SdlGraphicsBackend: Failed to allocate first framebuffer page\n");
      SDL_DestroyTexture(texture);
      SDL_DestroyRenderer(renderer);
      SDL_DestroyWindow(window);
      SDL_Quit();
      return false;
    }

    framebuffer = first_page.as<uint32_t>();

    // Allocate additional pages if needed
    for (uint32_t i = 1; i < pages_needed; i++) {
      PageAddr page = PageAddr((uintptr_t)ou_alloc_page());
      if (page.raw() == 0) {
        oprintf("SdlGraphicsBackend: Failed to allocate framebuffer page %u\n", i);
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return false;
      }
    }

    // Clear framebuffer to black
    for (uint32_t i = 0; i < width * height; i++) {
      framebuffer[i] = 0xFF000000; // Black with full alpha
    }

    oprintf("SdlGraphicsBackend: Initialized at 0x%lx\n", (uintptr_t)framebuffer);
    return true;
  }

  ~SdlGraphicsBackend() {
    if (texture) {
      SDL_DestroyTexture(texture);
    }
    if (renderer) {
      SDL_DestroyRenderer(renderer);
    }
    if (window) {
      SDL_DestroyWindow(window);
    }
    SDL_Quit();
  }

  uint32_t *get_framebuffer() override { return framebuffer; }

  void flush() override {
    if (!framebuffer || !texture || !renderer) {
      oprintf("SdlGraphicsBackend: Cannot flush - not initialized\n");
      return;
    }

    // Update texture from framebuffer
    SDL_UpdateTexture(texture, nullptr, framebuffer, width * sizeof(uint32_t));

    // Clear renderer
    SDL_RenderClear(renderer);

    // Copy texture to renderer
    SDL_RenderCopy(renderer, texture, nullptr, nullptr);

    // Present to screen
    SDL_RenderPresent(renderer);
  }

  uint32_t get_width() const override { return width; }

  uint32_t get_height() const override { return height; }
};

#endif
