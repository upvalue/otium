#ifndef OT_USER_FS_VIRTIO_DISK_HPP
#define OT_USER_FS_VIRTIO_DISK_HPP

#include "ot/user/fs/disk.hpp"
#include "ot/user/virtio/virtio-blk.hpp"
#include "ot/user/virtio/virtio.hpp"

/**
 * VirtIO block device implementation of Disk interface.
 * Manages its own memory allocation for queue and request buffers.
 */
class VirtioDisk : public Disk {
  VirtIODevice dev;
  VirtQueue queue;
  VirtioBlkRequest *request;
  uint64_t capacity_sectors;

public:
  /**
   * Factory method - scans for VirtIO block device and initializes.
   * Allocates necessary memory (queue pages + request buffer).
   * @return Result with VirtioDisk pointer on success, or error code
   */
  static Result<VirtioDisk *, ErrorCode> create();

  ErrorCode read_sector(uint64_t sector, uint8_t *buf) override;
  ErrorCode write_sector(uint64_t sector, const uint8_t *buf) override;
  uint64_t sector_count() const override { return capacity_sectors; }

private:
  VirtioDisk() : request(nullptr), capacity_sectors(0) {}

  /**
   * Internal helper for sector read/write operations.
   * @param sector Sector number to access
   * @param is_write true for write, false for read
   * @return NONE on success, error code on failure
   */
  ErrorCode do_sector_request(uint64_t sector, bool is_write);
};

#endif
