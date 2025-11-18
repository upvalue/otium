// gfx-util.cpp - Graphics utility implementation
#include "ot/lib/gfx-util.hpp"
#include "ot/lib/gfx-font-data.hpp"

namespace gfx {

void GfxUtil::draw_char(int x, int y, char c, uint32_t color, int scale) {
  // Only support printable ASCII
  if (c < 32 || c > 126) {
    return;
  }

  uint16_t glyph = FONT_GLYPHS[c - 32];

  // Check if this glyph has a vertical offset (for descenders)
  int offset_y = ((glyph >> 15) & 1) * scale;

  // Draw the glyph bitmap
  for (int gy = 0; gy < FONT_HEIGHT; gy++) {
    for (int gx = 0; gx < FONT_WIDTH; gx++) {
      int bit = gy * FONT_WIDTH + gx;

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

void GfxUtil::draw_text(int x, int y, const char *text, uint32_t color,
                        int scale) {
  int cursor_x = x;
  int cursor_y = y;

  for (int i = 0; text[i] != '\0'; i++) {
    char c = text[i];

    if (c == '\n') {
      // Move to next line
      cursor_y += (FONT_HEIGHT + 2) * scale;
      cursor_x = x;
    } else if (c == ' ') {
      // Space - just advance cursor
      cursor_x += FONT_ADVANCE * scale;
    } else {
      // Draw character
      draw_char(cursor_x, cursor_y, c, color, scale);
      cursor_x += FONT_ADVANCE * scale;
    }
  }
}

} // namespace gfx
