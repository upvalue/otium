/**
 * WASM filesystem implementation.
 *
 * This backend delegates all file operations to JavaScript via EM_JS calls.
 * In Node.js, files are read from fs-in/ and written to fs-out/.
 * In the browser, files are stored in memory (with optional IndexedDB persistence).
 */

#include "ot/lib/logger.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/fs/types.hpp"
#include "ot/user/gen/filesystem-server.hpp"
#include "ot/user/user.hpp"

#include <emscripten.h>

using namespace filesystem;

// ============================================================================
// EM_JS functions to call JavaScript for file operations
// ============================================================================

// clang-format off
// Check if a path exists and return its type: 0=not found, 1=file, 2=directory
EM_JS(int, js_fs_exists, (const char *path), {
  const pathStr = UTF8ToString(path);
  if (typeof Module.fsExists === 'function') {
    const result = Module.fsExists(pathStr);
    if (result === 'file') return 1;
    if (result === 'dir') return 2;
  }
  return 0;
});

// Read a file's contents. Returns bytes read, or -1 on error.
// Data is written to the provided buffer.
EM_JS(int, js_fs_read_file, (const char *path, uint8_t *buf, int max_len), {
  const pathStr = UTF8ToString(path);
  if (typeof Module.fsReadFile === 'function') {
    const data = Module.fsReadFile(pathStr);
    if (data === null || data === undefined) {
      return -1;
    }
    const len = Math.min(data.length, max_len);
    for (let i = 0; i < len; i++) {
      HEAPU8[buf + i] = data[i];
    }
    return len;
  }
  return -1;
});

// Get the size of a file. Returns -1 if file doesn't exist.
EM_JS(int, js_fs_file_size, (const char *path), {
  const pathStr = UTF8ToString(path);
  if (typeof Module.fsFileSize === 'function') {
    return Module.fsFileSize(pathStr);
  }
  // Fallback: read the file and get its length
  if (typeof Module.fsReadFile === 'function') {
    const data = Module.fsReadFile(pathStr);
    if (data === null || data === undefined) {
      return -1;
    }
    return data.length;
  }
  return -1;
});

// Write data to a file. Returns bytes written, or -1 on error.
EM_JS(int, js_fs_write_file, (const char *path, const uint8_t *data, int len), {
  const pathStr = UTF8ToString(path);
  if (typeof Module.fsWriteFile === 'function') {
    const arr = new Uint8Array(len);
    for (let i = 0; i < len; i++) {
      arr[i] = HEAPU8[data + i];
    }
    const success = Module.fsWriteFile(pathStr, arr);
    return success ? len : -1;
  }
  return -1;
});

// Create an empty file. Returns 1 on success, 0 on failure.
EM_JS(int, js_fs_create_file, (const char *path), {
  const pathStr = UTF8ToString(path);
  if (typeof Module.fsCreateFile === 'function') {
    return Module.fsCreateFile(pathStr) ? 1 : 0;
  }
  return 0;
});

// Create a directory. Returns 1 on success, 0 on failure.
EM_JS(int, js_fs_create_dir, (const char *path), {
  const pathStr = UTF8ToString(path);
  if (typeof Module.fsCreateDir === 'function') {
    return Module.fsCreateDir(pathStr) ? 1 : 0;
  }
  return 0;
});

// Delete a file. Returns 1 on success, 0 on failure.
EM_JS(int, js_fs_delete_file, (const char *path), {
  const pathStr = UTF8ToString(path);
  if (typeof Module.fsDeleteFile === 'function') {
    return Module.fsDeleteFile(pathStr) ? 1 : 0;
  }
  return 0;
});

// Delete a directory. Returns 1 on success, 0 on failure.
EM_JS(int, js_fs_delete_dir, (const char *path), {
  const pathStr = UTF8ToString(path);
  if (typeof Module.fsDeleteDir === 'function') {
    return Module.fsDeleteDir(pathStr) ? 1 : 0;
  }
  return 0;
});

// List directory contents. Returns the number of entries, or -1 on error.
// Each entry is written as a null-terminated string to buf, separated by nulls.
// If entry is a directory, it ends with '/'.
EM_JS(int, js_fs_list_dir, (const char *path, char *buf, int max_len), {
  const pathStr = UTF8ToString(path);
  if (typeof Module.fsListDir === 'function') {
    const entries = Module.fsListDir(pathStr);
    if (entries === null || entries === undefined) {
      return -1;
    }
    let offset = 0;
    for (let i = 0; i < entries.length; i++) {
      const entry = entries[i];
      if (offset + entry.length + 1 > max_len) {
        break; // Buffer full
      }
      for (let j = 0; j < entry.length; j++) {
        HEAPU8[buf + offset++] = entry.charCodeAt(j);
      }
      HEAPU8[buf + offset++] = 0; // Null terminator
    }
    return entries.length;
  }
  return -1;
});
// clang-format on

// ============================================================================
// WASM Filesystem Server
// ============================================================================

struct WasmFilesystemServer : LocalStorage, FilesystemServerBase {
  Logger l;

  // Open file handle tracking
  struct OpenFile {
    ou::string path;
    uintptr_t flags;
    bool in_use;
    size_t position; // Current read/write position
  };
  OpenFile open_files[MAX_OPEN_HANDLES];
  uint32_t next_handle_id;

  WasmFilesystemServer() : l("fs/wasm"), next_handle_id(1) {
    for (size_t i = 0; i < MAX_OPEN_HANDLES; i++) {
      open_files[i].in_use = false;
    }
  }

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

  OpenFile *allocate_file(uint32_t *out_handle_id) {
    for (size_t i = 0; i < MAX_OPEN_HANDLES; i++) {
      if (!open_files[i].in_use) {
        open_files[i].in_use = true;
        open_files[i].position = 0;
        *out_handle_id = (uint32_t)(i + 1);
        return &open_files[i];
      }
    }
    return nullptr;
  }

private:
  Result<FileHandleId, ErrorCode> handle_open(const ou::string &path, uintptr_t flags) override {
    // Check if file exists
    int exists = js_fs_exists(path.c_str());

    if (exists == 0) {
      // File doesn't exist
      if (flags & OPEN_CREATE) {
        // Create the file
        if (!js_fs_create_file(path.c_str())) {
          return Result<FileHandleId, ErrorCode>::err(FILESYSTEM__IO_ERROR);
        }
      } else {
        return Result<FileHandleId, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
      }
    } else if (exists == 2) {
      // It's a directory, can't open as file
      return Result<FileHandleId, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    // Handle truncate flag
    if ((flags & OPEN_TRUNCATE) && exists == 1) {
      // Truncate by writing empty content
      js_fs_write_file(path.c_str(), nullptr, 0);
    }

    // Allocate a handle
    uint32_t handle_id;
    OpenFile *of = allocate_file(&handle_id);
    if (!of) {
      return Result<FileHandleId, ErrorCode>::err(FILESYSTEM__TOO_MANY_OPEN_FILES);
    }

    of->path = path;
    of->flags = flags;
    of->position = 0;

    return Result<FileHandleId, ErrorCode>::ok(FileHandleId(handle_id));
  }

  Result<uintptr_t, ErrorCode> handle_read(FileHandleId handle_id, uintptr_t offset, uintptr_t length) override {
    OpenFile *of = find_open_file(handle_id.raw());
    if (!of) {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
    }

    // Get file size first
    int file_size = js_fs_file_size(of->path.c_str());
    if (file_size < 0) {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    if (offset >= (uintptr_t)file_size) {
      // Reading past end of file - return 0 bytes with empty msgpack
      PageAddr comm = ou_get_comm_page();
      MPackWriter writer(comm.as_ptr(), OT_PAGE_SIZE);
      writer.bin(nullptr, 0);
      return Result<uintptr_t, ErrorCode>::ok(0);
    }

    // Limit read size to comm page size minus MsgPack overhead
    size_t max_read = OT_PAGE_SIZE - 16;
    if (length > max_read) {
      length = max_read;
    }

    size_t available = file_size - offset;
    if (length > available) {
      length = available;
    }

    // Read file content into comm page
    PageAddr comm = ou_get_comm_page();
    uint8_t *buffer = (uint8_t *)comm.as_ptr() + 8; // Leave room for msgpack header

    // Read entire file first, then extract the portion we need
    // (JS doesn't have a seek+read, so we read all and slice)
    ou::vector<uint8_t> full_content;
    full_content.resize(file_size);

    int bytes_read = js_fs_read_file(of->path.c_str(), full_content.data(), file_size);
    if (bytes_read < 0) {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    // Copy the requested portion
    size_t actual_read = (length < (size_t)(bytes_read - offset)) ? length : (bytes_read - offset);
    for (size_t i = 0; i < actual_read; i++) {
      buffer[i] = full_content[offset + i];
    }

    // Write as msgpack binary
    MPackWriter writer(comm.as_ptr(), OT_PAGE_SIZE);
    writer.bin(buffer, actual_read);

    return Result<uintptr_t, ErrorCode>::ok(actual_read);
  }

  Result<uintptr_t, ErrorCode> handle_write(FileHandleId handle_id, uintptr_t offset, const StringView &data) override {
    OpenFile *of = find_open_file(handle_id.raw());
    if (!of) {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
    }

    // Read existing file content (if any)
    int file_size = js_fs_file_size(of->path.c_str());
    ou::vector<uint8_t> content;

    if (file_size > 0) {
      content.resize(file_size);
      js_fs_read_file(of->path.c_str(), content.data(), file_size);
    }

    // Expand if needed
    size_t required_size = offset + data.len;
    if (content.size() < required_size) {
      content.resize(required_size, 0);
    }

    // Write the new data at offset
    for (size_t i = 0; i < data.len; i++) {
      content[offset + i] = (uint8_t)data.ptr[i];
    }

    // Write back to JS
    int result = js_fs_write_file(of->path.c_str(), content.data(), content.size());
    if (result < 0) {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    return Result<uintptr_t, ErrorCode>::ok(data.len);
  }

  Result<bool, ErrorCode> handle_close(FileHandleId handle_id) override {
    OpenFile *of = find_open_file(handle_id.raw());
    if (!of) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
    }

    of->in_use = false;
    of->path.clear();

    return Result<bool, ErrorCode>::ok(true);
  }

  Result<bool, ErrorCode> handle_create_file(const ou::string &path) override {
    // Check if already exists
    int exists = js_fs_exists(path.c_str());
    if (exists != 0) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__ALREADY_EXISTS);
    }

    if (!js_fs_create_file(path.c_str())) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    return Result<bool, ErrorCode>::ok(true);
  }

  Result<bool, ErrorCode> handle_create_dir(const ou::string &path) override {
    // Check if already exists
    int exists = js_fs_exists(path.c_str());
    if (exists != 0) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__ALREADY_EXISTS);
    }

    if (!js_fs_create_dir(path.c_str())) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    return Result<bool, ErrorCode>::ok(true);
  }

  Result<bool, ErrorCode> handle_delete_file(const ou::string &path) override {
    int exists = js_fs_exists(path.c_str());
    if (exists != 1) { // Must be a file
      return Result<bool, ErrorCode>::err(FILESYSTEM__FILE_NOT_FOUND);
    }

    if (!js_fs_delete_file(path.c_str())) {
      return Result<bool, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    return Result<bool, ErrorCode>::ok(true);
  }

  Result<bool, ErrorCode> handle_delete_dir(const ou::string &path) override {
    int exists = js_fs_exists(path.c_str());
    if (exists != 2) { // Must be a directory
      return Result<bool, ErrorCode>::err(FILESYSTEM__DIR_NOT_FOUND);
    }

    if (!js_fs_delete_dir(path.c_str())) {
      // Could be not empty or IO error
      return Result<bool, ErrorCode>::err(FILESYSTEM__IO_ERROR);
    }

    return Result<bool, ErrorCode>::ok(true);
  }

  Result<uintptr_t, ErrorCode> handle_list_dir(const ou::string &path) override {
    // Handle empty path as root
    ou::string lookup_path = path.empty() ? "/" : path;

    // Check if path exists and is a directory
    int exists = js_fs_exists(lookup_path.c_str());
    if (exists != 2) { // Must be a directory
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__DIR_NOT_FOUND);
    }

    // Use scratch buffer to get directory listing from JS
    // Format: null-separated strings with entries ending in '/' for directories
    int count = js_fs_list_dir(lookup_path.c_str(), ot_scratch_buffer, OT_PAGE_SIZE);
    if (count < 0) {
      return Result<uintptr_t, ErrorCode>::err(FILESYSTEM__DIR_NOT_FOUND);
    }

    // Parse entries from scratch buffer and write to comm page as msgpack
    PageAddr comm = ou_get_comm_page();
    MPackWriter writer(comm.as_ptr(), OT_PAGE_SIZE);
    writer.array((uint32_t)count);

    // Parse null-separated strings from scratch buffer
    const char *ptr = ot_scratch_buffer;
    for (int i = 0; i < count; i++) {
      size_t len = strlen(ptr);
      writer.str(ptr, (uint32_t)len);
      ptr += len + 1; // Skip past null terminator
    }

    return Result<uintptr_t, ErrorCode>::ok((uintptr_t)count);
  }
};

void proc_filesystem(void) {
  void *storage_page = ou_get_storage().as_ptr();
  Logger l("fs/wasm");

  l.log("Starting WASM filesystem server");

  // Create server instance
  WasmFilesystemServer *server = new (storage_page) WasmFilesystemServer();
  server->process_storage_init(10);

  l.log("WASM filesystem server initialized");

  server->run();
}
