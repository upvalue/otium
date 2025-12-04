// app-framework.cpp - Application framework implementation
#include "ot/lib/app-framework.hpp"
#include "ot/lib/arena.hpp"
#include "ot/lib/font-blit16.hpp"
#include "ot/lib/font-proggy.hpp"
#include "ot/user/user.hpp"
#include "ot/vendor/libschrift/schrift.h"

// Use ou_malloc/ou_free for memory allocation
extern "C" void *ou_malloc(size_t size);
extern "C" void ou_free(void *ptr);

// Debug logging
extern "C" void oprintf(const char *fmt, ...);

namespace app {

Framework::Framework(uint32_t *framebuffer, int width, int height)
    : fb_(framebuffer), width_(width), height_(height), ttf_font_(nullptr), arena_memory_(nullptr), arena_(nullptr) {
  // Allocate pages for arena
  arena_memory_ = ou_alloc_page();
  for (size_t i = 1; i < ARENA_NUM_PAGES; i++) {
    ou_alloc_page(); // Allocate additional contiguous pages
  }

  if (arena_memory_) {
    // Placement-new the Arena object at the start of the allocated memory
    // The arena uses the rest of the memory for allocations
    size_t arena_obj_size = sizeof(lib::Arena);
    size_t total_size = ARENA_NUM_PAGES * OT_PAGE_SIZE;
    uint8_t *arena_start = (uint8_t *)arena_memory_ + arena_obj_size;
    size_t arena_size = total_size - arena_obj_size;

    arena_ = new (arena_memory_) lib::Arena(arena_start, arena_size);
    arena_->set_fallback(ou_malloc, ou_free);
  }
}

Framework::~Framework() {
  if (ttf_font_) {
    sft_freefont(ttf_font_);
    ttf_font_ = nullptr;
  }
  // Note: arena_ is placement-new'd into arena_memory_
  // arena_memory_ is OS pages that are freed when process exits
}

// === BLIT16 FONT METHODS ===

void Framework::draw_blit16_char(int x, int y, char c, uint32_t color, int scale) {
  // Only support printable ASCII
  if (c < 32 || c > 126) {
    return;
  }

  uint16_t glyph = blit16::FONT_GLYPHS[c - 32];

  // Check if this glyph has a vertical offset (for descenders)
  int offset_y = ((glyph >> 15) & 1) * scale;

  // Draw the glyph bitmap
  for (int gy = 0; gy < blit16::FONT_HEIGHT; gy++) {
    for (int gx = 0; gx < blit16::FONT_WIDTH; gx++) {
      int bit = gy * blit16::FONT_WIDTH + gx;

      // Check if this pixel is set in the glyph
      if ((glyph >> bit) & 1) {
        // Draw scaled pixel
        for (int sy = 0; sy < scale; sy++) {
          for (int sx = 0; sx < scale; sx++) {
            int px = x + gx * scale + sx;
            int py = y + gy * scale + sy + offset_y;
            put_pixel(px, py, color);
          }
        }
      }
    }
  }
}

void Framework::draw_blit16_text(int x, int y, const char *text, uint32_t color, int scale) {
  int cursor_x = x;
  int cursor_y = y;

  for (int i = 0; text[i] != '\0'; i++) {
    char c = text[i];

    if (c == '\n') {
      // Move to next line
      cursor_y += (blit16::FONT_HEIGHT + 2) * scale;
      cursor_x = x;
    } else if (c == ' ') {
      // Space - just advance cursor
      cursor_x += blit16::FONT_ADVANCE * scale;
    } else {
      // Draw character
      draw_blit16_char(cursor_x, cursor_y, c, color, scale);
      cursor_x += blit16::FONT_ADVANCE * scale;
    }
  }
}

// === TTF FONT METHODS ===

Result<bool, ErrorCode> Framework::init_ttf() {
  if (ttf_font_) {
    return Result<bool, ErrorCode>::ok(true); // Already initialized
  }

  ttf_font_ = sft_loadmem(proggy_font_data, proggy_font_size);
  if (!ttf_font_) {
    oprintf("[app] TTF font load failed\n");
    return Result<bool, ErrorCode>::err(APP__FONT_LOAD_FAILED);
  }

  oprintf("[app] TTF font loaded successfully\n");
  return Result<bool, ErrorCode>::ok(true);
}

void Framework::blend_pixel(int x, int y, uint32_t color, uint8_t alpha) {
  if (x < 0 || x >= width_ || y < 0 || y >= height_) {
    return;
  }
  if (alpha == 0) {
    return;
  }

  uint32_t *dst = &fb_[y * width_ + x];

  if (alpha == 255) {
    *dst = color;
    return;
  }

  // Extract source color components
  uint8_t src_b = color & 0xFF;
  uint8_t src_g = (color >> 8) & 0xFF;
  uint8_t src_r = (color >> 16) & 0xFF;

  // Extract destination color components
  uint8_t dst_b = *dst & 0xFF;
  uint8_t dst_g = (*dst >> 8) & 0xFF;
  uint8_t dst_r = (*dst >> 16) & 0xFF;

  // Alpha blend
  uint8_t out_r = (uint8_t)((src_r * alpha + dst_r * (255 - alpha)) / 255);
  uint8_t out_g = (uint8_t)((src_g * alpha + dst_g * (255 - alpha)) / 255);
  uint8_t out_b = (uint8_t)((src_b * alpha + dst_b * (255 - alpha)) / 255);

  *dst = 0xFF000000 | ((uint32_t)out_r << 16) | ((uint32_t)out_g << 8) | out_b;
}

Result<int, ErrorCode> Framework::draw_ttf_char(int x, int y, uint32_t codepoint, uint32_t color, int size_px) {
  if (!ttf_font_) {
    return Result<int, ErrorCode>::err(APP__FONT_NOT_LOADED);
  }

  SFT sft = {};
  sft.font = ttf_font_;
  sft.xScale = (float)size_px;
  sft.yScale = (float)size_px;
  sft.xOffset = 0;
  sft.yOffset = 0;
  sft.flags = SFT_DOWNWARD_Y;
  sft.arena = arena_; // Use arena allocator for outline data

  // Lookup glyph ID
  SFT_Glyph glyph;
  if (sft_lookup(&sft, codepoint, &glyph) < 0) {
    oprintf("[app] Glyph lookup failed for codepoint %u\n", codepoint);
    return Result<int, ErrorCode>::err(APP__GLYPH_LOOKUP_FAILED);
  }

  // Get glyph metrics
  SFT_GMetrics metrics;
  if (sft_gmetrics(&sft, glyph, &metrics) < 0) {
    oprintf("[app] Glyph metrics failed for codepoint %u\n", codepoint);
    return Result<int, ErrorCode>::err(APP__GLYPH_METRICS_FAILED);
  }

  int glyph_w = metrics.minWidth;
  int glyph_h = metrics.minHeight;

  if (glyph_w <= 0 || glyph_h <= 0) {
    // Space or empty glyph - just return advance
    return Result<int, ErrorCode>::ok((int)metrics.advanceWidth);
  }

  // Allocate buffer for glyph image
  uint8_t *pixels = (uint8_t *)ou_malloc((size_t)(glyph_w * glyph_h));
  if (!pixels) {
    oprintf("[app] Memory alloc failed for glyph %ux%u\n", glyph_w, glyph_h);
    return Result<int, ErrorCode>::err(APP__MEMORY_ALLOC_FAILED);
  }

  SFT_Image image = {};
  image.pixels = pixels;
  image.width = glyph_w;
  image.height = glyph_h;

  if (sft_render(&sft, glyph, image) < 0) {
    oprintf("[app] Glyph render failed for codepoint %u\n", codepoint);
    ou_free(pixels);
    return Result<int, ErrorCode>::err(APP__GLYPH_RENDER_FAILED);
  }

  // Get line metrics for baseline calculation
  SFT_LMetrics lmetrics;
  if (sft_lmetrics(&sft, &lmetrics) < 0) {
    ou_free(pixels);
    return Result<int, ErrorCode>::ok((int)metrics.advanceWidth);
  }

  // Calculate draw position
  int draw_x = x + (int)metrics.leftSideBearing;
  int draw_y = y + (int)lmetrics.ascender + metrics.yOffset;

  // Debug: count non-zero alpha pixels
  static int draw_log_count = 0;
  int nonzero_alpha = 0;

  // Blit to framebuffer with alpha blending
  for (int py = 0; py < glyph_h; py++) {
    for (int px = 0; px < glyph_w; px++) {
      uint8_t alpha = pixels[py * glyph_w + px];
      if (alpha > 0)
        nonzero_alpha++;
      blend_pixel(draw_x + px, draw_y + py, color, alpha);
    }
  }

  if (draw_log_count < 3) {
    oprintf("[app] blit: pos=(%d,%d) nonzero=%d\n", draw_x, draw_y, nonzero_alpha);
    draw_log_count++;
  }

  ou_free(pixels);

  // Reset arena for next glyph (frees any fallback allocations too)
  if (arena_) {
    arena_->free_fallbacks();
    arena_->reset();
  }

  return Result<int, ErrorCode>::ok((int)metrics.advanceWidth);
}

Result<int, ErrorCode> Framework::draw_ttf_text(int x, int y, const char *text, uint32_t color, int size_px) {
  if (!ttf_font_) {
    return Result<int, ErrorCode>::err(APP__FONT_NOT_LOADED);
  }
  if (!text) {
    return Result<int, ErrorCode>::ok(0);
  }

  int cursor_x = x;
  int start_x = x;

  // Simple ASCII iteration
  for (const char *p = text; *p; p++) {
    if (*p == '\n') {
      // Move to next line
      cursor_x = start_x;
      y += size_px + 2; // Line height
      continue;
    }

    auto result = draw_ttf_char(cursor_x, y, (uint32_t)(unsigned char)*p, color, size_px);
    if (result.is_err()) {
      return result; // Propagate error
    }
    cursor_x += result.value();
  }

  return Result<int, ErrorCode>::ok(cursor_x - start_x);
}

Result<int, ErrorCode> Framework::measure_ttf_text(const char *text, int size_px) {
  if (!ttf_font_) {
    return Result<int, ErrorCode>::err(APP__FONT_NOT_LOADED);
  }
  if (!text) {
    return Result<int, ErrorCode>::ok(0);
  }

  SFT sft = {};
  sft.font = ttf_font_;
  sft.xScale = (float)size_px;
  sft.yScale = (float)size_px;
  sft.flags = SFT_DOWNWARD_Y;

  int total_width = 0;
  for (const char *p = text; *p; p++) {
    if (*p == '\n') {
      continue; // Newlines don't contribute to width
    }

    SFT_Glyph glyph;
    if (sft_lookup(&sft, (uint32_t)(unsigned char)*p, &glyph) < 0) {
      continue;
    }

    SFT_GMetrics metrics;
    if (sft_gmetrics(&sft, glyph, &metrics) < 0) {
      continue;
    }

    total_width += (int)metrics.advanceWidth;
  }

  return Result<int, ErrorCode>::ok(total_width);
}

Result<int, ErrorCode> Framework::draw_ttf_text_wrapped(int x, int y, int max_width, const char *text, uint32_t color,
                                                        int size_px) {
  if (!ttf_font_) {
    return Result<int, ErrorCode>::err(APP__FONT_NOT_LOADED);
  }
  if (!text) {
    return Result<int, ErrorCode>::ok(0);
  }

  // Get line metrics for proper line height
  SFT sft = {};
  sft.font = ttf_font_;
  sft.xScale = (float)size_px;
  sft.yScale = (float)size_px;
  sft.flags = SFT_DOWNWARD_Y;
  sft.arena = arena_;

  SFT_LMetrics lmetrics;
  if (sft_lmetrics(&sft, &lmetrics) < 0) {
    return Result<int, ErrorCode>::err(APP__GLYPH_METRICS_FAILED);
  }
  int line_height = (int)(lmetrics.ascender - lmetrics.descender) + 2;

  int cursor_x = x;
  int cursor_y = y;
  int start_x = x;

  for (const char *p = text; *p; p++) {
    if (*p == '\n') {
      cursor_x = start_x;
      cursor_y += line_height;
      continue;
    }

    // Get glyph metrics to check if it fits
    SFT_Glyph glyph;
    if (sft_lookup(&sft, (uint32_t)(unsigned char)*p, &glyph) < 0) {
      continue;
    }

    SFT_GMetrics metrics;
    if (sft_gmetrics(&sft, glyph, &metrics) < 0) {
      continue;
    }

    int advance = (int)metrics.advanceWidth;

    // Wrap if this character would exceed max_width
    if (cursor_x + advance > start_x + max_width && cursor_x > start_x) {
      cursor_x = start_x;
      cursor_y += line_height;
    }

    // Draw the character
    auto result = draw_ttf_char(cursor_x, cursor_y, (uint32_t)(unsigned char)*p, color, size_px);
    if (result.is_ok()) {
      cursor_x += result.value();
    }
  }

  // Return total height used
  return Result<int, ErrorCode>::ok(cursor_y - y + line_height);
}

} // namespace app
