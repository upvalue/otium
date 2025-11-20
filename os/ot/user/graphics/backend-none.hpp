#pragma once

#include "ot/user/graphics/backend.hpp"

// Graphics backend that returns NOT_INITIALIZED for all operations
// Used when graphics support is disabled (OT_GRAPHICS_BACKEND_NONE)
class NoneGraphicsBackend : public GraphicsBackend {
 public:
  NoneGraphicsBackend() = default;
  ~NoneGraphicsBackend() override = default;

  bool init() override { return true; }  // "Successfully" do nothing

  uint32_t *get_framebuffer() override { return nullptr; }

  uint32_t get_width() const override { return 0; }

  uint32_t get_height() const override { return 0; }

  void flush() override {
    // Do nothing
  }
};
