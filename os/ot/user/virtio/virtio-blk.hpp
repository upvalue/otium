#ifndef OT_USER_VIRTIO_BLK_HPP
#define OT_USER_VIRTIO_BLK_HPP

#include "ot/user/virtio/virtio.hpp"

#define VIRTIO_BLK_REQUEST_TYPE_READ 0
#define VIRTIO_BLK_REQUEST_TYPE_WRITE 1

struct VirtioBlkRequestHeader {
  uint32_t type;
  uint32_t reserved;
  uint64_t sector;
} __attribute__((packed));

/**
 * VirtIO block request type
 * See section 5.2.6 of https://docs.oasis-open.org/virtio/virtio/v1.1/csprd01/virtio-v1.1-csprd01.html#x1-2390002
 */
struct VirtioBlkRequest {
  VirtioBlkRequestHeader header;
  uint8_t data[512];
  uint8_t status;
} __attribute__((packed));

#endif