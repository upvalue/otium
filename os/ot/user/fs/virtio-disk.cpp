#include "ot/user/fs/virtio-disk.hpp"
#include "ot/lib/logger.hpp"
#include "ot/user/user.hpp"
#include <string.h>

Result<VirtioDisk *, ErrorCode> VirtioDisk::create() {
  Logger l("disk/virtio");

  // Scan for VirtIO block device
  auto res = VirtIODevice::scan_for_device(VIRTIO_ID_BLOCK);
  if (!res.is_ok()) {
    l.log("VirtIO block device not found: %s", error_code_to_string(res.error()));
    return Result<VirtioDisk *, ErrorCode>::err(res.error());
  }

  // Allocate page for disk object
  PageAddr disk_page = PageAddr(ou_alloc_page());
  VirtioDisk *disk = new (disk_page.as_ptr()) VirtioDisk();

  // Initialize VirtIO device
  auto addr = res.value();
  disk->dev.set_base(addr);

  if (!disk->dev.init()) {
    l.log("VirtIO feature negotiation failed");
    return Result<VirtioDisk *, ErrorCode>::err(VIRTIO__SETUP_FAIL);
  }

  // Allocate 2 contiguous pages for legacy VirtIO queue (used ring must be page-aligned)
  PageAddr queue_mem = PageAddr((uintptr_t)ou_alloc_page());
  ou_alloc_page(); // Second page for used ring

  disk->dev.setup_queue(0, disk->queue, queue_mem, QUEUE_SIZE);
  disk->dev.set_driver_ok();

  // Allocate request buffer
  PageAddr block_buffer = PageAddr(ou_alloc_page());
  disk->request = new (block_buffer.as_ptr()) VirtioBlkRequest();

  // Read disk capacity (VirtIO block device capacity register at offset 0x100)
  uint32_t capacity_low = disk->dev.read_reg(0x100);
  disk->capacity_sectors = capacity_low;

  l.log("VirtIO block device initialized: %llu sectors (%llu bytes)", disk->capacity_sectors,
        disk->capacity_sectors * DISK_SECTOR_SIZE);

  return Result<VirtioDisk *, ErrorCode>::ok(disk);
}

bool VirtioDisk::do_sector_request(uint64_t sector, bool is_write) {
  VirtioBlkRequest &req = *request;
  req.header.sector = sector;
  req.header.type = is_write ? VIRTIO_BLK_REQUEST_TYPE_WRITE : VIRTIO_BLK_REQUEST_TYPE_READ;

  queue.chain()
      .out(PageAddr(&req.header), sizeof(req.header))
      .out_or_in(is_write, PageAddr(&req.data), DISK_SECTOR_SIZE)
      .in(PageAddr(&req.status), sizeof(req.status))
      .submit();

  dev.write_reg(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

  while (!queue.has_used()) {
    continue;
  }
  queue.get_used(); // Consume the used descriptor to update last_used_idx

  return req.status == 0;
}

bool VirtioDisk::read_sector(uint64_t sector, uint8_t *buf) {
  if (sector >= capacity_sectors) {
    return false;
  }

  if (!do_sector_request(sector, false)) {
    return false;
  }

  memcpy(buf, request->data, DISK_SECTOR_SIZE);
  return true;
}

bool VirtioDisk::write_sector(uint64_t sector, const uint8_t *buf) {
  if (sector >= capacity_sectors) {
    return false;
  }

  memcpy(request->data, buf, DISK_SECTOR_SIZE);

  if (!do_sector_request(sector, true)) {
    return false;
  }

  return true;
}
