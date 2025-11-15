#ifndef OT_KERNEL_GFX_HPP
#define OT_KERNEL_GFX_HPP

#include <stdint.h>

#include "ot/common.h"

// Color type: BGRA format (0xAARRGGBB)
typedef uint32_t Color;

// Helper macros for color construction
#define COLOR_BGRA(b, g, r, a) ((Color)(((a) << 24) | ((r) << 16) | ((g) << 8) | (b)))
#define COLOR_RGB(r, g, b) COLOR_BGRA(b, g, r, 0xFF)

/**
 * Abstract graphics interface.
 * Implementations provide platform-specific rendering backends.
 */
class Gfx {
public:
  virtual ~Gfx() {}

  /**
   * Initialize the graphics subsystem.
   * @return true on success, false on failure
   */
  virtual bool init() = 0;

  /**
   * Set a pixel at the given coordinates.
   * Out-of-bounds coordinates are silently clipped.
   * @param x X coordinate
   * @param y Y coordinate
   * @param color Color in BGRA format
   */
  virtual void put(uint32_t x, uint32_t y, Color color) = 0;

  /**
   * Fill a rectangle with a solid color.
   * @param x X coordinate of top-left corner
   * @param y Y coordinate of top-left corner
   * @param w Width of rectangle
   * @param h Height of rectangle
   * @param color Fill color in BGRA format
   */
  virtual void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, Color color) = 0;

  /**
   * Flush pending changes to the display.
   * Must be called to make drawing operations visible.
   */
  virtual void flush() = 0;

  /**
   * Get the width of the display in pixels.
   * @return Display width, or 0 if not initialized
   */
  virtual uint32_t get_width() const = 0;

  /**
   * Get the height of the display in pixels.
   * @return Display height, or 0 if not initialized
   */
  virtual uint32_t get_height() const = 0;
};

/**
 * Stub implementation for platforms without graphics support.
 * All operations are no-ops.
 */
class GfxUnsupported : public Gfx {
public:
  bool init() override {
    oprintf("Graphics not supported on this platform\n");
    return false;
  }

  void put(uint32_t x, uint32_t y, Color color) override {
    // No-op
    (void)x;
    (void)y;
    (void)color;
  }

  void fill_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h, Color color) override {
    // No-op
    (void)x;
    (void)y;
    (void)w;
    (void)h;
    (void)color;
  }

  void flush() override {
    // No-op
  }

  uint32_t get_width() const override { return 0; }

  uint32_t get_height() const override { return 0; }
};

#endif
