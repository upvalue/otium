#include "ot/lib/logger.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/filesystem/types.hpp"
#include "ot/user/gen/filesystem-server.hpp"
#include "ot/user/user.hpp"
#include "ot/user/virtio/virtio-blk.hpp"
#include "ot/user/virtio/virtio-debug.hpp"
#include "ot/user/virtio/virtio.hpp"
#include <string.h>

using namespace filesystem;

#define SECTOR_SIZE 512

struct OneFileServer : LocalStorage, FilesystemServerBase {
  VirtIODevice dev;
  VirtQueue queue;
  VirtioBlkRequest *request = nullptr;

  bool file_is_open = false;
  uint32_t current_handle_id = 1;
  char stored_filename[128] = {0};

  OneFileServer() {}

private:
  bool do_sector_request(bool is_write) {
    VirtioBlkRequest &req = *request;
    req.header.sector = 0;
    req.header.type = is_write ? VIRTIO_BLK_REQUEST_TYPE_WRITE : VIRTIO_BLK_REQUEST_TYPE_READ;

    queue.chain()
        .out(PageAddr(&req.header), sizeof(req.header))
        .out_or_in(is_write, PageAddr(&req.data), SECTOR_SIZE)
        .in(PageAddr(&req.status), sizeof(req.status))
        .submit();

    dev.write_reg(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

    while (!queue.has_used()) {
      continue;
    }
    queue.get_used(); // Consume the used descriptor to update last_used_idx

    return req.status == 0;
  }

  void dump_buffer(const uint8_t *buf, size_t len, const char *label) {
    oprintf("[onefile] %s (%u bytes):\n", label, (unsigned)len);
    size_t display_len = (len < 64) ? len : 64;
    for (size_t i = 0; i < display_len; i += 16) {
      oprintf("  %04x: ", (unsigned)i);
      for (size_t j = i; j < i + 16 && j < display_len; j++) {
        oprintf("%02x ", buf[j]);
      }
      // Pad if needed
      for (size_t j = display_len; j < i + 16; j++) {
        oprintf("   ");
      }
      oprintf(" | ");
      for (size_t j = i; j < i + 16 && j < display_len; j++) {
        char c = buf[j];
        oprintf("%c", (c >= 32 && c < 127) ? c : '.');
      }
      oprintf("\n");
    }
  }

  bool do_sector_read() {
    VirtioBlkRequest &req = *request;

    // Debug: Initial state
    oprintf("[onefile] === Starting sector read ===\n");
    virtio_debug::dump_queue_state(queue, "before read");

    // Setup request header
    req.header.sector = 0;
    req.header.type = VIRTIO_BLK_REQUEST_TYPE_READ;
    req.status = 0xFF; // Set to known value to verify device writes it

    // Clear data buffer with known pattern
    memset(req.data, 0xAA, SECTOR_SIZE);

    // Debug: Show addresses and setup
    oprintf("[onefile] Request addresses:\n");
    oprintf("  header @ %p (addr: 0x%lx)\n", &req.header, (uintptr_t)PageAddr(&req.header).raw());
    oprintf("  data   @ %p (addr: 0x%lx)\n", &req.data, (uintptr_t)PageAddr(&req.data).raw());
    oprintf("  status @ %p (addr: 0x%lx)\n", &req.status, (uintptr_t)PageAddr(&req.status).raw());
    oprintf("  header: type=%u, sector=%llu\n", req.header.type, req.header.sector);

    // Build descriptor chain
    queue.chain()
        .out(PageAddr(&req.header), sizeof(req.header))
        .in(PageAddr(&req.data), SECTOR_SIZE)
        .in(PageAddr(&req.status), sizeof(req.status))
        .submit();

    // Debug: Dump queue after submit
    virtio_debug::dump_queue_state(queue, "after submit");

    // Notify device
    dev.write_reg(VIRTIO_MMIO_QUEUE_NOTIFY, 0);
    oprintf("[onefile] Notified device, waiting for completion...\n");

    // Wait for device to complete (same as reference implementation)
    while (!queue.has_used()) {
      continue;
    }

    // Debug: After completion
    oprintf("[onefile] Device responded, status=%u\n", req.status);
    virtio_debug::dump_queue_state(queue, "after completion");

    // Consume used descriptor
    queue.get_used();

    // Debug: Dump data buffer
    dump_buffer(req.data, 64, "Read data");

    oprintf("[onefile] === Sector read complete, success=%d ===\n", req.status == 0);
    return req.status == 0;
  }

  Result<FileHandleId, ErrorCode> handle_open(const ou::string &path, uintptr_t flags) override {
    file_is_open = true;
    size_t copy_len = path.length() < 127 ? path.length() : 127;
    memcpy(stored_filename, path.c_str(), copy_len);
    stored_filename[copy_len] = '\0';
    return Result<FileHandleId, ErrorCode>::ok(FileHandleId(current_handle_id));
  }

  Result<uintptr_t, ErrorCode> handle_read(FileHandleId handle_id, uintptr_t offset, uintptr_t length) override {
    oprintf("[onefile] handle_read: handle=%u, offset=%u, length=%u, filename='%s'\n", (unsigned)handle_id.raw(),
            (unsigned)offset, (unsigned)length, stored_filename);

    if (offset != 0) {
      oprintf("[onefile] ERROR: non-zero offset not supported\n");
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    if (!do_sector_read()) {
      oprintf("[onefile] ERROR: sector read failed\n");
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    // Debug: Show full sector after successful read
    dump_buffer(request->data, 128, "Full sector after read");

    // Calculate where actual data starts (after filename + space)
    size_t filename_len = strlen(stored_filename);
    size_t data_start = 0;

    if (filename_len > 0 && filename_len < SECTOR_SIZE - 1) {
      data_start = filename_len + 1; // Skip "filename "
    }

    // Find content length starting from data region
    size_t content_len = 0;
    while (data_start + content_len < SECTOR_SIZE && request->data[data_start + content_len] != '\0') {
      content_len++;
    }

    size_t bytes_to_read = (length < content_len) ? length : content_len;
    oprintf("[onefile] read: filename_len=%u, data_start=%u, content_len=%u, bytes_to_read=%u\n",
            (unsigned)filename_len, (unsigned)data_start, (unsigned)content_len, (unsigned)bytes_to_read);

    PageAddr comm = ou_get_comm_page();
    oprintf("[onefile] comm_page=%p, writing %u bytes\n", comm.as_ptr(), (unsigned)bytes_to_read);
    MPackWriter writer(comm.as_ptr(), OT_PAGE_SIZE);
    writer.bin(request->data + data_start, bytes_to_read); // Skip filename!
    oprintf("[onefile] wrote to comm, first bytes: %02x %02x %02x %02x\n", ((uint8_t *)comm.as_ptr())[0],
            ((uint8_t *)comm.as_ptr())[1], ((uint8_t *)comm.as_ptr())[2], ((uint8_t *)comm.as_ptr())[3]);

    return Result<uintptr_t, ErrorCode>::ok(bytes_to_read);
  }

  Result<uintptr_t, ErrorCode> handle_write(FileHandleId handle_id, uintptr_t offset, const StringView &data) override {
    oprintf("[onefile] === Starting handle_write ===\n");
    oprintf("[onefile] handle_write: handle=%u, offset=%u, data_len=%u\n", (unsigned)handle_id.raw(), (unsigned)offset,
            (unsigned)data.len);

    if (!file_is_open) {
      oprintf("[onefile] ERROR: file not open\n");
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
    }

    if (offset != 0) {
      oprintf("[onefile] ERROR: non-zero offset not supported\n");
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    memset(request->data, 0, SECTOR_SIZE);

    size_t filename_len = strlen(stored_filename);
    size_t pos = 0;

    if (filename_len > 0 && filename_len < SECTOR_SIZE - 1) {
      memcpy(request->data, stored_filename, filename_len);
      pos = filename_len;
      request->data[pos++] = ' ';
    }

    size_t remaining = SECTOR_SIZE - pos;
    size_t content_len = (data.len < remaining) ? data.len : remaining;
    memcpy(request->data + pos, data.ptr, content_len);

    oprintf("[onefile] write: filename='%s', data_len=%u, total=%u\n", stored_filename, (unsigned)data.len,
            (unsigned)(pos + content_len));
    dump_buffer(request->data, 64, "Data to write");
    virtio_debug::dump_queue_state(queue, "before write");

    if (!do_sector_request(true)) {
      oprintf("[onefile] ERROR: sector write failed\n");
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    virtio_debug::dump_queue_state(queue, "after write");
    oprintf("[onefile] === Write successful ===\n");
    return Result<uintptr_t, ErrorCode>::ok(data.len);
  }

  Result<bool, ErrorCode> handle_close(FileHandleId handle_id) override {
    file_is_open = false;
    return Result<bool, ErrorCode>::ok(true);
  }

  Result<bool, ErrorCode> handle_create_file(const ou::string &path) override {
    return Result<bool, ErrorCode>::ok(true);
  }

  Result<bool, ErrorCode> handle_delete_file(const ou::string &path) override {
    return Result<bool, ErrorCode>::err(FILESYSTEM__IO_ERROR);
  }

  Result<bool, ErrorCode> handle_create_dir(const ou::string &path) override {
    return Result<bool, ErrorCode>::err(IPC__METHOD_NOT_IMPLEMENTED);
  }

  Result<bool, ErrorCode> handle_delete_dir(const ou::string &path) override {
    return Result<bool, ErrorCode>::err(IPC__METHOD_NOT_IMPLEMENTED);
  }
};

void proc_filesystem(void) {
  void *storage_page = ou_get_storage().as_ptr();
  Logger l("fs/onefile");

  OneFileServer *server = new (storage_page) OneFileServer();
  server->process_storage_init(10);
  // Allocate 2 contiguous pages for legacy VirtIO queue (used ring must be page-aligned)
  PageAddr queue_mem = PageAddr((uintptr_t)ou_alloc_page());
  ou_alloc_page(); // Second page for used ring

  auto res = VirtIODevice::scan_for_device(VIRTIO_ID_BLOCK);
  if (!res.is_ok()) {
    l.log("ERROR: VirtIO block device not found: %s\n", error_code_to_string(res.error()));
    ou_exit();
  }

  auto addr = res.value();
  VirtIODevice &dev = server->dev;
  dev.set_base(addr);

  if (!dev.init()) {
    l.log("ERROR: VirtIO feature negotiation failed");
    ou_exit();
  }

  dev.setup_queue(0, server->queue, queue_mem, QUEUE_SIZE);
  dev.set_driver_ok();

  PageAddr block_buffer = PageAddr(ou_alloc_page());
  if (block_buffer.is_null()) {
    l.log("ERROR: Failed to allocate block buffer");
    ou_exit();
  }
  server->request = new (block_buffer.as_ptr()) VirtioBlkRequest();

  server->run();
}
