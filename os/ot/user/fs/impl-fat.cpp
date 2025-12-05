#include "ot/lib/logger.hpp"
#include "ot/user/fs/disk.hpp"
#include "ot/user/fs/types.hpp"
#include "ot/user/fs/virtio-disk.hpp"
#include "ot/user/gen/filesystem-server.hpp"
#include "ot/user/user.hpp"

using namespace filesystem;

/**
 * FAT filesystem implementation (stub).
 * Currently unimplemented - all operations return errors.
 */
struct FatFilesystemServer : LocalStorage, FilesystemServerBase {
  Disk *disk = nullptr;

  FatFilesystemServer(Disk *d) : disk(d) {}

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
  void *storage_page = ou_get_storage().as_ptr();
  Logger l("fs/fat");

  // Create VirtIO disk
  auto disk_result = VirtioDisk::create();
  if (disk_result.is_err()) {
    l.log("ERROR: Failed to create VirtIO disk: %s", error_code_to_string(disk_result.error()));
    ou_exit();
  }

  Disk *disk = disk_result.value();
  l.log("FAT filesystem initialized (stub - not yet implemented)");
  l.log("Disk capacity: %llu sectors", disk->sector_count());

  // Create filesystem server with disk
  FatFilesystemServer *server = new (storage_page) FatFilesystemServer(disk);
  server->process_storage_init(10);

  server->run();
}
