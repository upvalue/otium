#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/filesystem/types.hpp"
#include "ot/user/gen/filesystem-server.hpp"
#include "ot/user/user.hpp"
#include "ot/user/virtio/virtio.hpp"

using namespace filesystem;

// Filesystem server implementation with instance state
struct FilesystemServer : FilesystemServerBase {
  FilesystemServer() {}

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
  // FilesystemStorage *fs_storage = new (storage_page) FilesystemStorage();

  LocalStorage *ls = new (storage_page) LocalStorage();
  ls->process_storage_init(10);

  auto res = VirtIODevice::scan_for_device(VIRTIO_ID_BLOCK);
  if (!res.is_ok()) {
    oprintf("ERROR: VirtIO block device not found: %s\n", error_code_to_string(res.error()));
    ou_exit();
  }

  auto addr = res.value();
  VirtIODevice dev(addr);
  dev.probe();
  dev.read_reg(VIRTIO_MMIO_DEVICE_ID);
  dev.read_reg(VIRTIO_MMIO_VENDOR_ID);
  dev.read_reg(VIRTIO_MMIO_DEVICE_FEATURES);
  dev.read_reg(VIRTIO_MMIO_DEVICE_FEATURES_SEL);
  dev.read_reg(VIRTIO_MMIO_DRIVER_FEATURES);
  dev.read_reg(VIRTIO_MMIO_DRIVER_FEATURES_SEL);

  // Create server and set storage pointer
  FilesystemServer server;
  // server.storage = fs_storage;

  // Run server
  server.run();
}
