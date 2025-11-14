#include "ot/drivers/drv-virtio.hpp"

void scan_virtio_devices() {
  oprintf("Scanning for VirtIO devices...\n\n");

  for (int i = 0; i < VIRTIO_MMIO_COUNT; i++) {
    uintptr_t addr = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE);
    VirtIODevice dev(addr);
    dev.probe();
  }
}
