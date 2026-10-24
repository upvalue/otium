#ifndef OTOS_VIRTIO_H
#define OTOS_VIRTIO_H

#include <stddef.h>
#include <stdint.h>

/* A virtio-mmio device (version 2) with one virtqueue, driven by polling: each
 * request is a single command and response that completes before returning. */
typedef struct virtio_device {
  volatile uint32_t* registers;
  struct virtq_desc* desc;
  struct virtq_avail* avail;
  struct virtq_used* used;
  uint16_t size;
  uint16_t last_used;
} virtio_device;

enum { VIRTIO_ID_GPU = 16 };

/* Finds the first device with the given ID on the QEMU virt machine's
 * virtio-mmio slots, negotiates no optional features and sets up queue 0. */
bool virtio_open(virtio_device* device, uint32_t id);
/* Sends command to the device and waits until it has written response. */
void virtio_request(virtio_device* device, const void* command, size_t command_length,
                    void* response, size_t response_length);

#endif
