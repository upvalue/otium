// frame-manager.cpp - Simple frame rate manager implementation
#include "ot/lib/frame-manager.hpp"

namespace graphics {

FrameManager::FrameManager(int target_fps) : frame_in_progress_(false) {
  // Calculate target frame duration in platform-specific time units
  // O_TIME_UNITS_PER_SECOND is defined per-platform (1000 for WASM, 10000000 for RISC-V)
  target_frame_duration_ = O_TIME_UNITS_PER_SECOND / target_fps;

  // Initialize to 0 so first frame always renders
  last_frame_time_ = 0;
}

bool FrameManager::begin_frame() {
  // If we're already in a frame, don't start another
  if (frame_in_progress_) {
    return false;
  }

  uint64_t current_time = o_time_get();

  // Check if enough time has passed since the last frame
  uint64_t elapsed = current_time - last_frame_time_;

  if (elapsed >= target_frame_duration_) {
    // Time to render a new frame
    last_frame_time_ = current_time;
    frame_in_progress_ = true;
    return true;
  }

  // Not enough time has passed, skip rendering
  return false;
}

void FrameManager::end_frame() {
  // Mark frame as complete
  frame_in_progress_ = false;
}

} // namespace graphics
