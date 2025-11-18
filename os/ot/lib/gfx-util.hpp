// gfx-util.hpp - Graphics utility class for framebuffer manipulation
#pragma once

#include "ot/common.h"

namespace gfx {

// Utility class for framebuffer graphics operations
class GfxUtil {
public:
  GfxUtil(uint32_t *framebuffer, int width, int height)
      : fb_(framebuffer), width_(width), height_(height) {}

  // Accessors
  int width() const { return width_; }
  int height() const { return height_; }
  uint32_t *framebuffer() const { return fb_; }

  // Clear framebuffer to a color
  void clear(uint32_t color = 0xFF000000) {
    int num_pixels = width_ * height_;
    for (int i = 0; i < num_pixels; i++) {
      fb_[i] = color;
    }
  }

  // Put a pixel at (x, y) with bounds checking
  void put_pixel(int x, int y, uint32_t color) {
    if (x >= 0 && x < width_ && y >= 0 && y < height_) {
      fb_[y * width_ + x] = color;
    }
  }

  // Get a pixel at (x, y) with bounds checking
  uint32_t get_pixel(int x, int y) const {
    if (x >= 0 && x < width_ && y >= 0 && y < height_) {
      return fb_[y * width_ + x];
    }
    return 0;
  }

  // Fill a rectangle
  void fill_rect(int x, int y, int w, int h, uint32_t color) {
    for (int dy = 0; dy < h; dy++) {
      for (int dx = 0; dx < w; dx++) {
        put_pixel(x + dx, y + dy, color);
      }
    }
  }

  // Draw a horizontal line
  void draw_hline(int x, int y, int length, uint32_t color) {
    for (int i = 0; i < length; i++) {
      put_pixel(x + i, y, color);
    }
  }

  // Interpolate between two BGRA colors (0.0 = start, 1.0 = end)
  static uint32_t interpolate_color(uint32_t start, uint32_t end, float t) {
    if (t <= 0.0f)
      return start;
    if (t >= 1.0f)
      return end;

    uint8_t start_b = start & 0xFF;
    uint8_t start_g = (start >> 8) & 0xFF;
    uint8_t start_r = (start >> 16) & 0xFF;
    uint8_t start_a = (start >> 24) & 0xFF;

    uint8_t end_b = end & 0xFF;
    uint8_t end_g = (end >> 8) & 0xFF;
    uint8_t end_r = (end >> 16) & 0xFF;
    uint8_t end_a = (end >> 24) & 0xFF;

    uint8_t b = (uint8_t)(start_b + t * (end_b - start_b));
    uint8_t g = (uint8_t)(start_g + t * (end_g - start_g));
    uint8_t r = (uint8_t)(start_r + t * (end_r - start_r));
    uint8_t a = (uint8_t)(start_a + t * (end_a - start_a));

    return (a << 24) | (r << 16) | (g << 8) | b;
  }

  // Draw a filled circle with gradient from center to edge
  void draw_gradient_circle(int cx, int cy, int radius, uint32_t center_color,
                            uint32_t edge_color) {
    int radius_sq = radius * radius;

    for (int dy = -radius; dy <= radius; dy++) {
      for (int dx = -radius; dx <= radius; dx++) {
        int dist_sq = dx * dx + dy * dy;

        if (dist_sq <= radius_sq) {
          int px = cx + dx;
          int py = cy + dy;

          if (px >= 0 && px < width_ && py >= 0 && py < height_) {
            // Calculate distance from center (0.0 = center, 1.0 = edge)
            float dist_ratio = (float)dist_sq / (float)radius_sq;

            // Interpolate from center to edge color
            uint32_t color =
                interpolate_color(center_color, edge_color, dist_ratio);

            fb_[py * width_ + px] = color;
          }
        }
      }
    }
  }

  // Font rendering
  void draw_char(int x, int y, char c, uint32_t color, int scale = 1);
  void draw_text(int x, int y, const char *text, uint32_t color, int scale = 1);

private:
  uint32_t *fb_;
  int width_;
  int height_;
};

} // namespace gfx
