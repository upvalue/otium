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

struct TarFSServer : LocalStorage, FilesystemServerBase {
  VirtIODevice dev;
  VirtQueue queue;

  VirtioBlkRequest *request = nullptr;

  TarFSServer() {}

private:
  Result<FileHandleId, ErrorCode> handle_open(const ou::string &path, uintptr_t flags) override {
    return Result<FileHandleId, ErrorCode>::err(IPC__METHOD_NOT_IMPLEMENTED);
  }

  Result<uintptr_t, ErrorCode> handle_read(FileHandleId handle_id, uintptr_t offset, uintptr_t length) override {
    return Result<uintptr_t, ErrorCode>::err(IPC__METHOD_NOT_IMPLEMENTED);
  }

  Result<uintptr_t, ErrorCode> handle_write(FileHandleId handle_id, uintptr_t offset, const StringView &data) override {
    return Result<uintptr_t, ErrorCode>::err(IPC__METHOD_NOT_IMPLEMENTED);
  }

  Result<bool, ErrorCode> handle_close(FileHandleId handle_id) override {
    return Result<bool, ErrorCode>::err(IPC__METHOD_NOT_IMPLEMENTED);
  }

  Result<bool, ErrorCode> handle_create_file(const ou::string &path) override {
    return Result<bool, ErrorCode>::err(IPC__METHOD_NOT_IMPLEMENTED);
  }

  Result<bool, ErrorCode> handle_delete_file(const ou::string &path) override {
    return Result<bool, ErrorCode>::err(IPC__METHOD_NOT_IMPLEMENTED);
  }

  Result<bool, ErrorCode> handle_create_dir(const ou::string &path) override {
    return Result<bool, ErrorCode>::err(IPC__METHOD_NOT_IMPLEMENTED);
  }

  Result<bool, ErrorCode> handle_delete_dir(const ou::string &path) override {

    return Result<bool, ErrorCode>::err(IPC__METHOD_NOT_IMPLEMENTED);
  }
};

void test_rw_request(TarFSServer &srv, bool is_write) {
  Logger l("fs/tar");

  l.log("Testing %s request", is_write ? "write" : "read");
  VirtioBlkRequest &req = *(srv.request);
  req.header.sector = 0;
  req.header.type = is_write ? VIRTIO_BLK_REQUEST_TYPE_WRITE : VIRTIO_BLK_REQUEST_TYPE_READ;
  VirtQueue &q = srv.queue;
  q.chain()
      .out(PageAddr(&req.header), sizeof(req.header))
      .out_or_in(is_write, PageAddr(&req.data), SECTOR_SIZE)
      .in(PageAddr(&req.status), sizeof(req.status))
      .submit();

  srv.dev.write_reg(VIRTIO_MMIO_QUEUE_NOTIFY, 0);

  while (!q.has_used()) {
    continue;
  }

  if (req.status != 0) {
    oprintf("ERROR: VirtIO block device request failed: %u\n", req.status);
    ou_exit();
  }
  oprintf("Request successful\n");
}

void proc_filesystem(void) {
  // Initialize storage
  void *storage_page = ou_get_storage().as_ptr();
  Logger l("fs/tar");
  // FilesystemStorage *fs_storage = new (storage_page) FilesystemStorage();

  // TarFSStorage *ls = new (storage_page) TarFSStorage();
  TarFSServer *server = new (storage_page) TarFSServer();
  server->process_storage_init(0);
  // Allocate 2 pages for legacy VirtIO queue (used ring must be page-aligned)
  PageAddr queue_mem = PageAddr((uintptr_t)ou_alloc_page());
  ou_alloc_page(); // Second page for used ring
  // ls->process_storage_init(10);

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

  // read block
  uint32_t block_capacity = dev.read_reg(0x100) * SECTOR_SIZE;
  oprintf("Block capacity: %u\n", block_capacity);

  // Create server and set storage pointer
  // server.storage = fs_storage;
  PageAddr block_buffer = PageAddr(ou_alloc_page());
  if (block_buffer.is_null()) {
    l.log("ERROR: Failed to allocate block buffer");
    ou_exit();
  }
  server->request = new (block_buffer.as_ptr()) VirtioBlkRequest();

  const char *test_string = "TEST FROM THE OPERATING SYSTEM 2.0 meowdy";
  memset(server->request->data, 0, SECTOR_SIZE);
  memcpy(server->request->data, test_string, strlen(test_string));
  test_rw_request(*server, true);

  // Run server
  server->run();
}
