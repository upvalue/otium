// frame-manager.hpp - Simple frame rate manager for graphical applications
#pragma once

#include "ot/common.h"

namespace graphics {

// Simple frame rate manager that helps applications maintain consistent frame timing
// while cooperating with other processes in the OS.
//
// Usage:
//   FrameManager fm(30); // Target 30 FPS
//   while (running) {
//     if (fm.begin_frame()) {
//       // Render frame
//       graphics_client.flush();
//       fm.end_frame();
//     }
//     ou_yield(); // Yield to other processes
//   }
class FrameManager {
public:
  // Create a frame manager with the specified target FPS
  explicit FrameManager(int target_fps);

  // Check if it's time to render a new frame.
  // Returns true if enough time has passed since the last frame.
  // If true, call end_frame() after rendering.
  bool begin_frame();

  // Mark the end of frame rendering.
  // Call this after rendering is complete.
  void end_frame();

private:
  uint64_t target_frame_duration_; // Target duration per frame in time units
  uint64_t last_frame_time_;       // Time when last frame started
  bool frame_in_progress_;         // Whether we're currently in a frame
};

} // namespace graphics
