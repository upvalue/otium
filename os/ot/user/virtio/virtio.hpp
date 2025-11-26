#ifndef OT_USER_VIRTIO_HPP
#define OT_USER_VIRTIO_HPP

#include <stdint.h>

#include "ot/common.h"
#include "ot/lib/address.hpp"
#include "ot/lib/error-codes.hpp"
#include "ot/user/user.hpp"

// VirtIO MMIO register offsets
#define VIRTIO_MMIO_MAGIC_VALUE 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00c
#define VIRTIO_MMIO_DEVICE_FEATURES 0x010
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
#define VIRTIO_MMIO_DRIVER_FEATURES 0x020
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028 // Legacy only
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03c // Legacy only
#define VIRTIO_MMIO_QUEUE_PFN 0x040   // Legacy only
#define VIRTIO_MMIO_QUEUE_READY 0x044 // Modern only
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_STATUS 0x070
#define VIRTIO_MMIO_QUEUE_DESC_LOW 0x080    // Modern only
#define VIRTIO_MMIO_QUEUE_DESC_HIGH 0x084   // Modern only
#define VIRTIO_MMIO_QUEUE_DRIVER_LOW 0x090  // Modern only
#define VIRTIO_MMIO_QUEUE_DRIVER_HIGH 0x094 // Modern only
#define VIRTIO_MMIO_QUEUE_DEVICE_LOW 0x0a0  // Modern only
#define VIRTIO_MMIO_QUEUE_DEVICE_HIGH 0x0a4 // Modern only

// expected values from registers
#define VIRTIO_MMIO_MAGIC_VALUE_EXPECTED 0x74726976
#define VIRTIO_MMIO_VERSION_EXPECTED 1

// VirtIO status bits
#define VIRTIO_STATUS_ACKNOWLEDGE 1
#define VIRTIO_STATUS_DRIVER 2
#define VIRTIO_STATUS_DRIVER_OK 4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED 128

// VirtIO device IDs
#define VIRTIO_ID_NETWORK 1
#define VIRTIO_ID_BLOCK 2
#define VIRTIO_ID_GPU 16

// QEMU RISC-V virt machine MMIO range
#define VIRTIO_MMIO_BASE 0x10001000
#define VIRTIO_MMIO_SIZE 0x1000
#define VIRTIO_MMIO_COUNT 8

// Virtqueue descriptor flags
#define VIRTQ_DESC_F_NEXT 1
#define VIRTQ_DESC_F_WRITE 2

#define QUEUE_SIZE 8

// Virtqueue descriptor
struct virtq_desc {
  uint64_t addr;
  uint32_t len;
  uint16_t flags;
  uint16_t next;
} __attribute__((packed));

// Virtqueue available ring
struct virtq_avail {
  uint16_t flags;
  uint16_t idx;
  uint16_t ring[QUEUE_SIZE];
  uint16_t used_event;
} __attribute__((packed));

// Virtqueue used element
struct virtq_used_elem {
  uint32_t id;
  uint32_t len;
} __attribute__((packed));

// Virtqueue used ring
struct virtq_used {
  uint16_t flags;
  uint16_t idx;
  struct virtq_used_elem ring[QUEUE_SIZE];
  uint16_t avail_event;
} __attribute__((packed));

class VirtIODevice {
private:
  volatile uint32_t *base;

public:
  uint32_t device_id;
  uint32_t vendor_id;

  VirtIODevice() : base(nullptr), device_id(0), vendor_id(0) {}
  VirtIODevice(uintptr_t addr) : base((volatile uint32_t *)addr), device_id(0), vendor_id(0) {}

  uint32_t read_reg(uint32_t offset) { return base[offset / 4]; }

  void set_base(uintptr_t addr) { base = (volatile uint32_t *)addr; }

  void write_reg(uint32_t offset, uint32_t value) { base[offset / 4] = value; }

  bool is_valid() {
    uint32_t magic = read_reg(VIRTIO_MMIO_MAGIC_VALUE);
    return magic == 0x74726976; // "virt" in little-endian
  }

  void probe() {
    if (!is_valid()) {
      return;
    }

    uint32_t version = read_reg(VIRTIO_MMIO_VERSION);
    device_id = read_reg(VIRTIO_MMIO_DEVICE_ID);
    vendor_id = read_reg(VIRTIO_MMIO_VENDOR_ID);
    uint32_t features = read_reg(VIRTIO_MMIO_DEVICE_FEATURES);

    oprintf("VirtIO Device at 0x%lx:\n", (uintptr_t)base);
    oprintf("  Magic: 0x%x\n", read_reg(VIRTIO_MMIO_MAGIC_VALUE));
    oprintf("  Version: %u\n", version);
    oprintf("  Device ID: %u", device_id);

    // Print device type
    switch (device_id) {
    case VIRTIO_ID_GPU:
      oprintf(" (GPU)\n");
      break;
    case VIRTIO_ID_NETWORK:
      oprintf(" (Network)\n");
      break;
    case VIRTIO_ID_BLOCK:
      oprintf(" (Block)\n");
      break;
    default:
      oprintf(" (Unknown)\n");
      break;
    }

    oprintf("  Vendor ID: 0x%x\n", vendor_id);
    oprintf("  Features: 0x%x\n", features);
  }

  static uint32_t read_base_reg(uintptr_t offset) { return ((volatile uint32_t *)VIRTIO_MMIO_BASE)[offset / 4]; }

  /**
   * Scans for a VirtIO device by ID return address if found
   */
  static Result<uintptr_t, ErrorCode> scan_for_device(uintptr_t device_id) {
    // sanity checks

    if (read_base_reg(VIRTIO_MMIO_MAGIC_VALUE) != VIRTIO_MMIO_MAGIC_VALUE_EXPECTED) {
      return Result<uintptr_t, ErrorCode>::err(VIRTIO__SETUP_FAIL);
    }

    if (read_base_reg(VIRTIO_MMIO_VERSION) != VIRTIO_MMIO_VERSION_EXPECTED) {
      return Result<uintptr_t, ErrorCode>::err(VIRTIO__SETUP_FAIL);
    }

    //
    for (int i = 0; i < VIRTIO_MMIO_COUNT; i++) {
      uintptr_t addr = VIRTIO_MMIO_BASE + (i * VIRTIO_MMIO_SIZE);
      VirtIODevice dev(addr);
      dev.device_id = dev.read_reg(VIRTIO_MMIO_DEVICE_ID);
      if (dev.is_valid() && dev.device_id == device_id) {
        return Result<uintptr_t, ErrorCode>::ok(addr);
      }
    }
    return Result<uintptr_t, ErrorCode>::err(VIRTIO__DEVICE_NOT_FOUND);
  }
};

class VirtQueue {
public:
  struct virtq_desc *desc;
  struct virtq_avail *avail;
  struct virtq_used *used;
  uint16_t last_used_idx;
  uint16_t queue_size;

  void init(PageAddr mem, uint16_t size) {
    queue_size = size;
    last_used_idx = 0;

    // Layout: descriptors, then avail ring, then used ring
    // VirtIO requires: desc (16-byte aligned), avail (2-byte), used (4-byte)
    desc = mem.as<struct virtq_desc>();
    avail = (struct virtq_avail *)(desc + size);

    // Calculate used ring address with proper 4-byte alignment
    uintptr_t used_addr = (uintptr_t)avail + sizeof(struct virtq_avail) + sizeof(uint16_t) * size;
    used_addr = (used_addr + 3) & ~3; // Round up to 4-byte boundary
    used = (struct virtq_used *)used_addr;

    // Zero out the queue
    omemset(desc, 0, sizeof(struct virtq_desc) * size);
    omemset(avail, 0, sizeof(struct virtq_avail) + sizeof(uint16_t) * size);
    omemset(used, 0, sizeof(struct virtq_used) + sizeof(struct virtq_used_elem) * size);
  }

  void add_buf(uint16_t desc_idx, PageAddr buf, uint32_t len, bool write) {
    // buf is already a physical address (PageAddr), use it directly
    desc[desc_idx].addr = (uint64_t)buf.raw();
    desc[desc_idx].len = len;
    desc[desc_idx].flags = write ? VIRTQ_DESC_F_WRITE : 0;
    desc[desc_idx].next = 0;
  }

  void submit(uint16_t desc_idx) {
    uint16_t idx = avail->idx % queue_size;
    avail->ring[idx] = desc_idx;
    avail->idx++;
  }

  bool has_used() { return last_used_idx != used->idx; }

  uint32_t get_used() {
    if (!has_used())
      return 0xFFFFFFFF;

    uint16_t idx = last_used_idx % queue_size;
    last_used_idx++;
    return used->ring[idx].id;
  }
};

#endif
