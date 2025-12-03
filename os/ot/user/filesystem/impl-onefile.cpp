#include "ot/lib/logger.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/filesystem/types.hpp"
#include "ot/user/gen/filesystem-server.hpp"
#include "ot/user/user.hpp"
#include "ot/user/virtio/virtio-blk.hpp"
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
    queue.get_used();  // Consume the used descriptor to update last_used_idx

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
    if (offset != 0) {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    // Clear buffer before read to see what device actually returns
    memset(request->data, 0xAA, SECTOR_SIZE);

    if (!do_sector_request(false)) {
      oprintf("[onefile] sector read failed\n");
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    // Calculate where actual data starts (after filename + space)
    size_t filename_len = strlen(stored_filename);
    size_t data_start = 0;

    if (filename_len > 0 && filename_len < SECTOR_SIZE - 1) {
      data_start = filename_len + 1;  // Skip "filename "
    }

    // Find content length starting from data region
    size_t content_len = 0;
    while (data_start + content_len < SECTOR_SIZE &&
           request->data[data_start + content_len] != '\0') {
      content_len++;
    }

    size_t bytes_to_read = (length < content_len) ? length : content_len;
    oprintf("[onefile] read: filename_len=%u, data_start=%u, content_len=%u, bytes_to_read=%u\n",
            (unsigned)filename_len, (unsigned)data_start, (unsigned)content_len, (unsigned)bytes_to_read);

    PageAddr comm = ou_get_comm_page();
    oprintf("[onefile] comm_page=%p, writing %u bytes\n", comm.as_ptr(), (unsigned)bytes_to_read);
    MPackWriter writer(comm.as_ptr(), OT_PAGE_SIZE);
    writer.bin(request->data + data_start, bytes_to_read);  // Skip filename!
    oprintf("[onefile] wrote to comm, first bytes: %02x %02x %02x %02x\n",
            ((uint8_t*)comm.as_ptr())[0], ((uint8_t*)comm.as_ptr())[1],
            ((uint8_t*)comm.as_ptr())[2], ((uint8_t*)comm.as_ptr())[3]);

    return Result<uintptr_t, ErrorCode>::ok(bytes_to_read);
  }

  Result<uintptr_t, ErrorCode> handle_write(FileHandleId handle_id, uintptr_t offset, const StringView &data) override {
    if (!file_is_open) {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
    }

    if (offset != 0) {
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

    oprintf("[onefile] write: filename='%s', data_len=%u, total=%u, first_byte=%02x\n",
            stored_filename, (unsigned)data.len, (unsigned)(pos + content_len), request->data[0]);

    if (!do_sector_request(true)) {
      oprintf("[onefile] sector write failed\n");
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    oprintf("[onefile] write successful\n");
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
  PageAddr queue_mem = PageAddr((uintptr_t)ou_alloc_page());

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
