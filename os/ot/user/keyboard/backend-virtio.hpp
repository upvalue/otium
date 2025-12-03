#ifndef OT_USER_KEYBOARD_BACKEND_VIRTIO_HPP
#define OT_USER_KEYBOARD_BACKEND_VIRTIO_HPP

#include "ot/lib/logger.hpp"
#include "ot/user/keyboard/backend.hpp"
#include "ot/user/user.hpp"
#include "ot/user/virtio/virtio.hpp"

// VirtIO device ID for input devices
#define VIRTIO_ID_INPUT 18

// VirtIO input event types
#define VIRTIO_INPUT_EV_KEY 1

// VirtIO input event structure (matches virtio spec)
struct virtio_input_event {
  uint16_t type;   // Event type (EV_KEY = 1)
  uint16_t code;   // Key code
  uint32_t value;  // 1 = press, 0 = release
} __attribute__((packed));

// Number of buffers to pre-post for receiving events
#define KEYBOARD_EVENT_BUFFERS 8

class VirtioKeyboardBackend : public KeyboardBackend {
public:
  VirtIODevice dev;
  VirtQueue eventq;              // Queue 0: event queue
  PageAddr event_buffers;        // Pre-posted buffers for events
  PageAddr queue_memory;         // Memory for virtqueue structures
  int next_buffer;               // Next buffer index to check
  bool shift_held;               // Modifier key state
  bool ctrl_held;
  bool alt_held;
  Logger l;

  VirtioKeyboardBackend() : dev(0), next_buffer(0), shift_held(false), ctrl_held(false), alt_held(false), l("kbd") {}

  VirtioKeyboardBackend(uintptr_t addr)
      : dev(addr), next_buffer(0), shift_held(false), ctrl_held(false), alt_held(false), l("kbd") {}

  bool init() override;
  bool poll_key(KeyEvent *out_event) override;

private:
  void post_buffers();  // Post empty buffers for device to fill
  void process_raw_event(const virtio_input_event *ev, KeyEvent *out_event);
};

#endif
