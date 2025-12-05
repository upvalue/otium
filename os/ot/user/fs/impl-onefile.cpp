#include "ot/lib/logger.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/fs/disk.hpp"
#include "ot/user/fs/virtio-disk.hpp"
#include "ot/user/fs/types.hpp"
#include "ot/user/gen/filesystem-server.hpp"
#include "ot/user/user.hpp"
#include <string.h>

using namespace filesystem;

struct OneFileServer : LocalStorage, FilesystemServerBase {
  Disk *disk = nullptr;

  bool file_is_open = false;
  uint32_t current_handle_id = 1;
  char stored_filename[128] = {0};

  OneFileServer(Disk *d) : disk(d) {}

private:

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

    // Read sector 0
    uint8_t sector_buf[DISK_SECTOR_SIZE];
    if (!disk->read_sector(0, sector_buf)) {
      oprintf("[onefile] ERROR: sector read failed\n");
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    // Calculate where actual data starts (after filename + space)
    size_t filename_len = strlen(stored_filename);
    size_t data_start = 0;

    if (filename_len > 0 && filename_len < DISK_SECTOR_SIZE - 1) {
      data_start = filename_len + 1; // Skip "filename "
    }

    // Find content length starting from data region
    size_t content_len = 0;
    while (data_start + content_len < DISK_SECTOR_SIZE && sector_buf[data_start + content_len] != '\0') {
      content_len++;
    }

    size_t bytes_to_read = (length < content_len) ? length : content_len;
    oprintf("[onefile] read: filename_len=%u, data_start=%u, content_len=%u, bytes_to_read=%u\n",
            (unsigned)filename_len, (unsigned)data_start, (unsigned)content_len, (unsigned)bytes_to_read);

    PageAddr comm = ou_get_comm_page();
    MPackWriter writer(comm.as_ptr(), OT_PAGE_SIZE);
    writer.bin(sector_buf + data_start, bytes_to_read);

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

    uint8_t sector_buf[DISK_SECTOR_SIZE];
    memset(sector_buf, 0, DISK_SECTOR_SIZE);

    size_t filename_len = strlen(stored_filename);
    size_t pos = 0;

    if (filename_len > 0 && filename_len < DISK_SECTOR_SIZE - 1) {
      memcpy(sector_buf, stored_filename, filename_len);
      pos = filename_len;
      sector_buf[pos++] = ' ';
    }

    size_t remaining = DISK_SECTOR_SIZE - pos;
    size_t content_len = (data.len < remaining) ? data.len : remaining;
    memcpy(sector_buf + pos, data.ptr, content_len);

    oprintf("[onefile] write: filename='%s', data_len=%u, total=%u\n", stored_filename, (unsigned)data.len,
            (unsigned)(pos + content_len));

    if (!disk->write_sector(0, sector_buf)) {
      oprintf("[onefile] ERROR: sector write failed\n");
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

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

  // Create VirtIO disk
  auto disk_result = VirtioDisk::create();
  if (disk_result.is_err()) {
    l.log("ERROR: Failed to create VirtIO disk: %s", error_code_to_string(disk_result.error()));
    ou_exit();
  }

  Disk *disk = disk_result.value();

  // Create filesystem server with disk
  OneFileServer *server = new (storage_page) OneFileServer(disk);
  server->process_storage_init(10);

  server->run();
}
