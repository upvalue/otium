#include "ot/user/keyboard/backend-virtio.hpp"

bool VirtioKeyboardBackend::init() {
  // Validate device
  if (!dev.is_valid() || dev.read_reg(VIRTIO_MMIO_DEVICE_ID) != VIRTIO_ID_INPUT) {
    l.log("ERROR: Invalid VirtIO input device");
    return false;
  }

  // Initialize device status sequence
  if (!dev.init()) {
    l.log("ERROR: Failed to initialize VirtIO device");
    return false;
  }

  // Check queue availability
  uint32_t queue_max = dev.read_reg(VIRTIO_MMIO_QUEUE_NUM_MAX);
  if (queue_max < QUEUE_SIZE) {
    l.log("ERROR: Queue too small (max=%u, need=%u)", queue_max, QUEUE_SIZE);
    return false;
  }

  // Allocate memory for virtqueue structures (2 pages for legacy VirtIO)
  queue_memory = PageAddr((uintptr_t)ou_alloc_page());
  if (queue_memory.raw() == 0) {
    l.log("ERROR: Failed to allocate queue memory");
    return false;
  }
  ou_alloc_page(); // Second page for used ring (page-aligned in legacy VirtIO)

  // Setup event queue (queue 0)
  dev.setup_queue(0, eventq, queue_memory, QUEUE_SIZE);

  // Mark device ready
  dev.set_driver_ok();

  // Allocate buffers for receiving events
  event_buffers = PageAddr((uintptr_t)ou_alloc_page());
  if (event_buffers.raw() == 0) {
    l.log("ERROR: Failed to allocate event buffers");
    return false;
  }

  // Post empty buffers for device to fill with events
  post_buffers();

  l.log("VirtIO keyboard initialized (eventq=%p, buffers=%p)", (void *)queue_memory.raw(), (void *)event_buffers.raw());

  return true;
}

void VirtioKeyboardBackend::post_buffers() {
  // Post KEYBOARD_EVENT_BUFFERS empty buffers to the event queue
  for (int i = 0; i < KEYBOARD_EVENT_BUFFERS; i++) {
    PageAddr buf_addr = event_buffers + (i * sizeof(virtio_input_event));

    // Clear buffer
    memset(buf_addr.as_ptr(), 0, sizeof(virtio_input_event));

    // Add to queue (device-writable - device fills these)
    eventq.chain(i).in(buf_addr, sizeof(virtio_input_event)).submit();
  }

  // Notify device that buffers are available
  dev.write_reg(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

  l.log("Posted %d event buffers", KEYBOARD_EVENT_BUFFERS);
}

void VirtioKeyboardBackend::process_raw_event(const virtio_input_event *ev, KeyEvent *out_event) {
  uint16_t code = ev->code;
  bool pressed = (ev->value == 1);

  // Update modifier state
  if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
    shift_held = pressed;
    // Don't report modifier keys as separate events
    return;
  }
  if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
    ctrl_held = pressed;
    return;
  }
  if (code == KEY_LEFTALT || code == KEY_RIGHTALT) {
    alt_held = pressed;
    return;
  }

  // Fill out event with key and current modifier state
  out_event->code = code;
  out_event->flags = 0;
  out_event->reserved = 0;

  if (pressed) {
    out_event->flags |= KEY_FLAG_PRESSED;
  }
  if (shift_held) {
    out_event->flags |= KEY_FLAG_SHIFT;
  }
  if (ctrl_held) {
    out_event->flags |= KEY_FLAG_CTRL;
  }
  if (alt_held) {
    out_event->flags |= KEY_FLAG_ALT;
  }
}

bool VirtioKeyboardBackend::poll_key(KeyEvent *out_event) {
  // Check if any used buffers are available
  if (!eventq.has_used()) {
    return false; // No events available
  }

  // Get the used buffer
  uint32_t desc_idx = eventq.get_used();
  if (desc_idx >= KEYBOARD_EVENT_BUFFERS) {
    l.log("ERROR: Invalid descriptor index %u", desc_idx);
    return false;
  }

  // Get the event data from the buffer
  PageAddr buf_addr = event_buffers + (desc_idx * sizeof(virtio_input_event));
  const virtio_input_event *ev = buf_addr.as<virtio_input_event>();

  // Process only keyboard events (EV_KEY)
  bool has_event = false;
  if (ev->type == VIRTIO_INPUT_EV_KEY) {
    process_raw_event(ev, out_event);

    // Only report non-modifier keys
    uint16_t code = ev->code;
    if (code != KEY_LEFTSHIFT && code != KEY_RIGHTSHIFT && code != KEY_LEFTCTRL && code != KEY_RIGHTCTRL &&
        code != KEY_LEFTALT && code != KEY_RIGHTALT) {
      has_event = true;
    }
  }

  // Re-post the buffer for reuse
  memset(buf_addr.as_ptr(), 0, sizeof(virtio_input_event));
  eventq.chain(desc_idx).in(buf_addr, sizeof(virtio_input_event)).submit();
  dev.write_reg(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

  return has_event;
}
