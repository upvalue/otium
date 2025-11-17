#ifndef OT_USER_GRAPHICS_BACKEND_HPP
#define OT_USER_GRAPHICS_BACKEND_HPP

#include <stdint.h>

#include "ot/core/drivers/gfx.hpp"

/**
 * Abstract graphics backend interface.
 * Implementations provide platform-specific rendering backends for user-space.
 */
class GraphicsBackend {
public:
  virtual ~GraphicsBackend() {}

  /**
   * Initialize the graphics backend.
   * @return true on success, false on failure
   */
  virtual bool init() = 0;

  /**
   * Get pointer to the framebuffer.
   * @return Pointer to framebuffer (BGRA format), or nullptr if not initialized
   */
  virtual uint32_t *get_framebuffer() = 0;

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

#endif
