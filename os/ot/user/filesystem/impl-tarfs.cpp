#include "ot/lib/logger.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/filesystem/types.hpp"
#include "ot/user/gen/filesystem-server.hpp"
#include "ot/user/user.hpp"
#include "ot/user/virtio/virtio.hpp"

using namespace filesystem;

struct TarFSServer : LocalStorage, FilesystemServerBase {
  VirtIODevice dev;
  VirtQueue queue;

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

void proc_filesystem(void) {
  // Initialize storage
  void *storage_page = ou_get_storage().as_ptr();
  Logger l("fs/tar");
  // FilesystemStorage *fs_storage = new (storage_page) FilesystemStorage();

  // TarFSStorage *ls = new (storage_page) TarFSStorage();
  TarFSServer *server = new (storage_page) TarFSServer();
  server->process_storage_init(1);
  PageAddr queue_mem = PageAddr((uintptr_t)ou_alloc_page());
  // ls->process_storage_init(10);

  auto res = VirtIODevice::scan_for_device(VIRTIO_ID_BLOCK);
  if (!res.is_ok()) {
    l.log("ERROR: VirtIO block device not found: %s\n", error_code_to_string(res.error()));
    ou_exit();
  }

  auto addr = res.value();
  VirtIODevice &dev = server->dev;
  dev.set_base(addr);

  dev.write_reg(VIRTIO_MMIO_STATUS, 0);
  dev.write_reg(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
  dev.write_reg(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
  dev.write_reg(VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_FEATURES_OK);

  uint32_t version = dev.read_reg(VIRTIO_MMIO_VERSION);

  if (version != 1) {
    l.log("ERROR: Unsupported VirtIO version: %u", version);
    ou_exit();
  }

  VirtQueue &q = server->queue;
  q.init(queue_mem, QUEUE_SIZE);

  l.log("Queue physical addr: 0x%x", queue_mem.raw());

  if (!(dev.read_reg(VIRTIO_MMIO_STATUS) & VIRTIO_STATUS_FEATURES_OK)) {
    oprintf("ERROR: VirtIO block device feature negotiation failed\n");
    ou_exit();
  }

  // Create server and set storage pointer
  // server.storage = fs_storage;

  // Run server
  server->run();
}
