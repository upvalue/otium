#include "virtio.h"

#include <stdlib.h>
#include <string.h>

/* QEMU's virt machine has eight virtio-mmio slots. The device tree lists
 * them too, but nothing else here reads it yet. */
#define MMIO_BASE 0x10001000u
#define MMIO_STRIDE 0x1000u
#define MMIO_SLOTS 8

/* Register offsets, in 32-bit words. */
enum {
  MAGIC = 0x000 / 4,
  VERSION = 0x004 / 4,
  DEVICE_ID = 0x008 / 4,
  DEVICE_FEATURES = 0x010 / 4,
  DEVICE_FEATURES_SEL = 0x014 / 4,
  DRIVER_FEATURES = 0x020 / 4,
  DRIVER_FEATURES_SEL = 0x024 / 4,
  QUEUE_SEL = 0x030 / 4,
  QUEUE_NUM_MAX = 0x034 / 4,
  QUEUE_NUM = 0x038 / 4,
  QUEUE_READY = 0x044 / 4,
  QUEUE_NOTIFY = 0x050 / 4,
  STATUS = 0x070 / 4,
  QUEUE_DESC_LOW = 0x080 / 4,
  QUEUE_DESC_HIGH = 0x084 / 4,
  QUEUE_DRIVER_LOW = 0x090 / 4,
  QUEUE_DRIVER_HIGH = 0x094 / 4,
  QUEUE_DEVICE_LOW = 0x0a0 / 4,
  QUEUE_DEVICE_HIGH = 0x0a4 / 4,
};

enum {
  STATUS_ACKNOWLEDGE = 1,
  STATUS_DRIVER = 2,
  STATUS_DRIVER_OK = 4,
  STATUS_FEATURES_OK = 8,
};

#define MAGIC_VALUE 0x74726976u /* "virt" */
#define QUEUE_SIZE 8

struct virtq_desc {
  uint64_t address;
  uint32_t length;
  uint16_t flags;
  uint16_t next;
};

enum { DESC_NEXT = 1, DESC_WRITE = 2 };

struct virtq_avail {
  uint16_t flags;
  uint16_t index;
  uint16_t ring[QUEUE_SIZE];
};

struct virtq_used {
  uint16_t flags;
  uint16_t index;
  struct {
    uint32_t id;
    uint32_t length;
  } ring[QUEUE_SIZE];
};

static void* zeroed(size_t alignment, size_t size) {
  void* memory = aligned_alloc(alignment, (size + alignment - 1) / alignment * alignment);
  if (memory != NULL) memset(memory, 0, size);
  return memory;
}

bool virtio_open(virtio_device* device, uint32_t id) {
  volatile uint32_t* r = NULL;
  for (uint32_t slot = 0; slot < MMIO_SLOTS && r == NULL; slot++) {
    volatile uint32_t* candidate = (volatile uint32_t*)(uintptr_t)(MMIO_BASE + slot * MMIO_STRIDE);
    if (candidate[MAGIC] == MAGIC_VALUE && candidate[VERSION] == 2 && candidate[DEVICE_ID] == id)
      r = candidate;
  }
  if (r == NULL) return false;

  r[STATUS] = 0;
  r[STATUS] = STATUS_ACKNOWLEDGE;
  r[STATUS] = STATUS_ACKNOWLEDGE | STATUS_DRIVER;
  /* Accept VIRTIO_F_VERSION_1 (feature bit 32) and nothing else. */
  r[DEVICE_FEATURES_SEL] = 1;
  if (!(r[DEVICE_FEATURES] & 1)) return false;
  r[DRIVER_FEATURES_SEL] = 0;
  r[DRIVER_FEATURES] = 0;
  r[DRIVER_FEATURES_SEL] = 1;
  r[DRIVER_FEATURES] = 1;
  r[STATUS] = STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK;
  if (!(r[STATUS] & STATUS_FEATURES_OK)) return false;

  r[QUEUE_SEL] = 0;
  if (r[QUEUE_NUM_MAX] < QUEUE_SIZE) return false;
  device->desc = zeroed(16, sizeof(struct virtq_desc) * QUEUE_SIZE);
  device->avail = zeroed(2, sizeof(struct virtq_avail));
  device->used = zeroed(4, sizeof(struct virtq_used));
  if (device->desc == NULL || device->avail == NULL || device->used == NULL) return false;
  device->registers = r;
  device->size = QUEUE_SIZE;
  device->last_used = 0;
  r[QUEUE_NUM] = QUEUE_SIZE;
  r[QUEUE_DESC_LOW] = (uint32_t)(uintptr_t)device->desc;
  r[QUEUE_DESC_HIGH] = 0;
  r[QUEUE_DRIVER_LOW] = (uint32_t)(uintptr_t)device->avail;
  r[QUEUE_DRIVER_HIGH] = 0;
  r[QUEUE_DEVICE_LOW] = (uint32_t)(uintptr_t)device->used;
  r[QUEUE_DEVICE_HIGH] = 0;
  r[QUEUE_READY] = 1;
  r[STATUS] = STATUS_ACKNOWLEDGE | STATUS_DRIVER | STATUS_FEATURES_OK | STATUS_DRIVER_OK;
  return true;
}

void virtio_request(virtio_device* device, const void* command, size_t command_length,
                    void* response, size_t response_length) {
  device->desc[0] = (struct virtq_desc){
      .address = (uintptr_t)command, .length = (uint32_t)command_length, .flags = DESC_NEXT, .next = 1};
  device->desc[1] = (struct virtq_desc){
      .address = (uintptr_t)response, .length = (uint32_t)response_length, .flags = DESC_WRITE};
  device->avail->ring[device->avail->index % device->size] = 0;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  device->avail->index++;
  __atomic_thread_fence(__ATOMIC_SEQ_CST);
  device->registers[QUEUE_NOTIFY] = 0;
  while (__atomic_load_n(&device->used->index, __ATOMIC_ACQUIRE) == device->last_used) {
  }
  device->last_used++;
}
