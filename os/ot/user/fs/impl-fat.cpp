#include "ot/lib/logger.hpp"
#include "ot/user/fs/disk.hpp"
#include "ot/user/fs/types.hpp"
#include "ot/user/fs/virtio-disk.hpp"
#include "ot/user/gen/filesystem-server.hpp"
#include "ot/user/user.hpp"

using namespace filesystem;

// https://wiki.osdev.org/FAT#FAT_32

struct FatBoot {
  // There's a lot of BIOS parameters which we don't care about
  // here

  // offset 0
  uint8_t jmp[3];
  // offset 3
  uint8_t _oem_name[8];
  // offset 11
  uint16_t bytes_per_sector;
  // 13
  uint8_t sectors_per_cluster;
  // 14
  uint16_t reserved_sectors;
  // 16
  uint8_t fat_count;
  // 17
  uint16_t root_entry_count;
  // 19
  uint16_t total_sectors_16;
  // 21
  uint8_t media_type;
  // 22
  uint16_t fat_size_16;
  // 24
  uint16_t sectors_per_track;
  // 26
  uint16_t head_count;
  // 28
  uint32_t hidden_sectors;
  // 32
  uint32_t total_sectors_32;
} __attribute__((packed));

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

  l.log("Starting FAT filesystem initialization");

  // Create VirtIO disk
  auto disk_result = VirtioDisk::create();
  if (disk_result.is_err()) {
    l.log("ERROR: Failed to create VirtIO disk: %s", error_code_to_string(disk_result.error()));
    ou_exit();
  }

  Disk *disk = disk_result.value();

  // Begin by sanity checking boot information
  uint8_t *buffer_page = (uint8_t *)ou_alloc_page();

  ErrorCode disk_err = disk->read_sector(0, buffer_page);

  if (disk_err != NONE) {
    l.log("ERROR: Failed to read boot sector: %s", error_code_to_string(disk_err));
    ou_exit();
  }

  FatBoot *boot = (FatBoot *)buffer_page;

  char oem_name[9];
  memcpy(oem_name, boot->_oem_name, 8);
  oem_name[8] = '\0';

  l.log("OEM name: %s", oem_name);
  l.log("Bytes per sector: %d", boot->bytes_per_sector);

  l.log("Sectors per cluster: %d", boot->sectors_per_cluster);

  l.log("FAT filesystem initialized (stub - not yet implemented)");
  l.log("Disk capacity: %llu sectors", disk->sector_count());

  if (boot->bytes_per_sector != DISK_SECTOR_SIZE) {
    l.log("ERROR: Bytes per sector is not equal to %d", DISK_SECTOR_SIZE);
    ou_exit();
  }

  // Create filesystem server with disk
  FatFilesystemServer *server = new (storage_page) FatFilesystemServer(disk);
  server->process_storage_init(10);

  server->run();
}
