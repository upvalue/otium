/**
 * FAT filesystem implementation using FatFs library.
 */

#include "ot/lib/logger.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/fs/disk.hpp"
#include "ot/user/fs/types.hpp"
#include "ot/user/fs/virtio-disk.hpp"
#include "ot/user/gen/filesystem-server.hpp"
#include "ot/user/user.hpp"

// FatFs headers
extern "C" {
#include "ot/vendor/fatfs/ff.h"
}

// Declare the disk setter from fatfs-diskio.cpp
void fatfs_set_disk(Disk *disk);

using namespace filesystem;

/**
 * Convert FatFs FRESULT to our ErrorCode.
 */
static ErrorCode fresult_to_error(FRESULT fr) {
  switch (fr) {
  case FR_OK:
    return NONE;
  case FR_NO_FILE:
  case FR_NO_PATH:
    return FILESYSTEM__FILE_NOT_FOUND;
  case FR_EXIST:
    return FILESYSTEM__ALREADY_EXISTS;
  case FR_INVALID_NAME:
  case FR_INVALID_PARAMETER:
    return FILESYSTEM__PATH_TOO_LONG;
  case FR_DENIED:
  case FR_WRITE_PROTECTED:
    return FILESYSTEM__IO_ERROR;
  case FR_TOO_MANY_OPEN_FILES:
    return FILESYSTEM__TOO_MANY_OPEN_FILES;
  case FR_DISK_ERR:
  case FR_INT_ERR:
  case FR_NOT_READY:
  case FR_NOT_ENABLED:
  case FR_NO_FILESYSTEM:
  default:
    return FILESYSTEM__IO_ERROR;
  }
}

/**
 * Convert our open flags to FatFs mode flags.
 */
static BYTE flags_to_fatfs_mode(uintptr_t flags) {
  BYTE mode = 0;

  if (flags & OPEN_READ) {
    mode |= FA_READ;
  }
  if (flags & OPEN_WRITE) {
    mode |= FA_WRITE;
  }
  if (flags & OPEN_CREATE) {
    mode |= FA_OPEN_ALWAYS; // Create if not exists, open if exists
  }
  if (flags & OPEN_TRUNCATE) {
    mode |= FA_CREATE_ALWAYS; // Always create new (truncate if exists)
  }

  // Default to read if no flags specified
  if (mode == 0) {
    mode = FA_READ;
  }

  return mode;
}

/**
 * Convert path from our format (starting with /) to FatFs format.
 * FatFs expects paths like "file.txt" or "dir/file.txt" for drive 0.
 */
static const char *convert_path(const ou::string &path) {
  if (path.length() > 0 && path[0] == '/') {
    return path.c_str() + 1; // Skip leading slash
  }
  return path.c_str();
}

/**
 * FAT filesystem server implementation.
 */
struct FatFilesystemServer : LocalStorage, FilesystemServerBase {
  Disk *disk = nullptr;
  FATFS fatfs;
  Logger l;

  // File handle management
  struct OpenFile {
    FIL fil;
    bool in_use;
    uintptr_t flags;
  };
  OpenFile open_files[MAX_OPEN_HANDLES];
  uint32_t next_handle_id;

  FatFilesystemServer(Disk *d) : disk(d), l("fs/fat"), next_handle_id(1) {
    // Initialize handle slots
    for (size_t i = 0; i < MAX_OPEN_HANDLES; i++) {
      open_files[i].in_use = false;
    }
  }

  // Find an open file by handle ID
  OpenFile *find_open_file(uint32_t handle_id) {
    if (handle_id == 0 || handle_id > MAX_OPEN_HANDLES) {
      return nullptr;
    }
    size_t idx = handle_id - 1;
    if (idx < MAX_OPEN_HANDLES && open_files[idx].in_use) {
      return &open_files[idx];
    }
    return nullptr;
  }

  // Allocate a new file handle slot
  OpenFile *allocate_file(uint32_t *out_handle_id) {
    for (size_t i = 0; i < MAX_OPEN_HANDLES; i++) {
      if (!open_files[i].in_use) {
        open_files[i].in_use = true;
        *out_handle_id = (uint32_t)(i + 1);
        return &open_files[i];
      }
    }
    return nullptr;
  }

private:
  Result<FileHandleId, ErrorCode> handle_open(const ou::string &path, uintptr_t flags) override {
    uint32_t handle_id;
    OpenFile *of = allocate_file(&handle_id);
    if (!of) {
      return Result<FileHandleId, ErrorCode>::err(FILESYSTEM__TOO_MANY_OPEN_FILES);
    }

    BYTE mode = flags_to_fatfs_mode(flags);
    const char *fpath = convert_path(path);

    FRESULT fr = f_open(&of->fil, fpath, mode);
    if (fr != FR_OK) {
      of->in_use = false;
      return Result<FileHandleId, ErrorCode>::err(fresult_to_error(fr));
    }

    of->flags = flags;
    return Result<FileHandleId, ErrorCode>::ok(FileHandleId(handle_id));
  }

  Result<uintptr_t, ErrorCode> handle_read(FileHandleId handle_id, uintptr_t offset, uintptr_t length) override {
    OpenFile *of = find_open_file(handle_id.raw());
    if (!of) {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
    }

    // Seek to offset
    FRESULT fr = f_lseek(&of->fil, offset);
    if (fr != FR_OK) {
      return Result<uintptr_t, ErrorCode>::err(fresult_to_error(fr));
    }

    // Read into comm page
    PageAddr comm = ou_get_comm_page();
    uint8_t *buffer = (uint8_t *)comm.as_ptr();

    // Limit read size to comm page size minus MsgPack overhead
    size_t max_read = OT_PAGE_SIZE - 16; // Leave room for msgpack header
    if (length > max_read) {
      length = max_read;
    }

    UINT bytes_read = 0;
    fr = f_read(&of->fil, buffer + 8, length, &bytes_read);
    if (fr != FR_OK) {
      return Result<uintptr_t, ErrorCode>::err(fresult_to_error(fr));
    }

    // Write as msgpack binary
    MPackWriter writer(comm.as_ptr(), OT_PAGE_SIZE);
    writer.bin(buffer + 8, bytes_read);

    return Result<uintptr_t, ErrorCode>::ok(bytes_read);
  }

  Result<uintptr_t, ErrorCode> handle_write(FileHandleId handle_id, uintptr_t offset, const StringView &data) override {
    OpenFile *of = find_open_file(handle_id.raw());
    if (!of) {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
    }

    // Seek to offset
    FRESULT fr = f_lseek(&of->fil, offset);
    if (fr != FR_OK) {
      return Result<uintptr_t, ErrorCode>::err(fresult_to_error(fr));
    }

    // Write data
    UINT bytes_written = 0;
    fr = f_write(&of->fil, data.ptr, data.len, &bytes_written);
    if (fr != FR_OK) {
      return Result<uintptr_t, ErrorCode>::err(fresult_to_error(fr));
    }

    // Sync to disk
    f_sync(&of->fil);

    return Result<uintptr_t, ErrorCode>::ok(bytes_written);
  }

  Result<bool, ErrorCode> handle_close(FileHandleId handle_id) override {
    OpenFile *of = find_open_file(handle_id.raw());
    if (!of) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
    }

    FRESULT fr = f_close(&of->fil);
    of->in_use = false;

    if (fr != FR_OK) {
      return Result<bool, ErrorCode>::err(fresult_to_error(fr));
    }

    return Result<bool, ErrorCode>::ok(true);
  }

  Result<bool, ErrorCode> handle_create_file(const ou::string &path) override {
    const char *fpath = convert_path(path);

    // Check if file already exists
    FILINFO fno;
    if (f_stat(fpath, &fno) == FR_OK) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__ALREADY_EXISTS);
    }

    // Create empty file
    FIL fil;
    FRESULT fr = f_open(&fil, fpath, FA_CREATE_NEW | FA_WRITE);
    if (fr != FR_OK) {
      return Result<bool, ErrorCode>::err(fresult_to_error(fr));
    }

    f_close(&fil);
    return Result<bool, ErrorCode>::ok(true);
  }

  Result<bool, ErrorCode> handle_delete_file(const ou::string &path) override {
    const char *fpath = convert_path(path);

    // Check if it exists and is a file
    FILINFO fno;
    FRESULT fr = f_stat(fpath, &fno);
    if (fr != FR_OK) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
    }

    if (fno.fattrib & AM_DIR) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND); // It's a directory
    }

    fr = f_unlink(fpath);
    if (fr != FR_OK) {
      return Result<bool, ErrorCode>::err(fresult_to_error(fr));
    }

    return Result<bool, ErrorCode>::ok(true);
  }

  Result<bool, ErrorCode> handle_create_dir(const ou::string &path) override {
    const char *fpath = convert_path(path);

    FRESULT fr = f_mkdir(fpath);
    if (fr == FR_EXIST) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__ALREADY_EXISTS);
    }
    if (fr != FR_OK) {
      return Result<bool, ErrorCode>::err(fresult_to_error(fr));
    }

    return Result<bool, ErrorCode>::ok(true);
  }

  Result<bool, ErrorCode> handle_delete_dir(const ou::string &path) override {
    const char *fpath = convert_path(path);

    // Check if it exists and is a directory
    FILINFO fno;
    FRESULT fr = f_stat(fpath, &fno);
    if (fr != FR_OK) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__DIR_NOT_FOUND);
    }

    if (!(fno.fattrib & AM_DIR)) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__DIR_NOT_FOUND); // It's a file
    }

    // f_unlink works for empty directories
    fr = f_unlink(fpath);
    if (fr == FR_DENIED) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__NOT_EMPTY);
    }
    if (fr != FR_OK) {
      return Result<bool, ErrorCode>::err(fresult_to_error(fr));
    }

    return Result<bool, ErrorCode>::ok(true);
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
  l.log("VirtIO disk created, capacity: %llu sectors", disk->sector_count());

  // Set up disk for FatFs
  fatfs_set_disk(disk);

  // Create server instance
  FatFilesystemServer *server = new (storage_page) FatFilesystemServer(disk);
  server->process_storage_init(10);

  // Mount FAT filesystem
  FRESULT fr = f_mount(&server->fatfs, "", 1); // Mount with immediate mount
  if (fr != FR_OK) {
    l.log("ERROR: Failed to mount FAT filesystem: %d", fr);
    ou_exit();
  }

  l.log("FAT filesystem mounted successfully");

  server->run();
}
