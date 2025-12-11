#include "ot/lib/file.hpp"
#include "ot/lib/mpack/mpack-reader.hpp"
#include "ot/user/gen/filesystem-client.hpp"
#include "ot/user/user.hpp"
#include "ot/user/fs/types.hpp"

namespace ou {

#ifndef OT_POSIX

// Static cache for filesystem server PID
static Pid g_fs_pid = PID_NONE;

File::File(const char *path, FileMode mode) : path_(path), mode_(mode), opened(false), fs_pid(PID_NONE), handle(0), write_offset_(0) {}

File::~File() {
  if (opened) {
    // Close the file handle
    FilesystemClient client(fs_pid);
    client.close(FileHandleId(handle));
  }
}

ErrorCode File::open() {
  // Look up filesystem server PID if not cached
  if (g_fs_pid == PID_NONE) {
    g_fs_pid = ou_proc_lookup("filesystem");
    if (g_fs_pid == PID_NONE) {
      return IPC__PID_NOT_FOUND;
    }
  }
  fs_pid = g_fs_pid;

  // Map FileMode to filesystem flags
  uintptr_t flags = 0;
  switch (mode_) {
  case FileMode::READ:
    flags = filesystem::OPEN_READ;
    break;
  case FileMode::WRITE:
    // Write mode: create if doesn't exist, truncate if exists, allow writing
    flags = filesystem::OPEN_WRITE | filesystem::OPEN_CREATE | filesystem::OPEN_TRUNCATE;
    break;
  case FileMode::APPEND:
    // Append mode: create if doesn't exist, don't truncate, allow writing
    flags = filesystem::OPEN_WRITE | filesystem::OPEN_CREATE;
    break;
  }

  // Open file via IPC
  FilesystemClient client(fs_pid);
  auto result = client.open(path_, flags);
  if (result.is_err()) {
    return result.error();
  }

  handle = result.value().raw();
  opened = true;
  return NONE;
}

Result<char, ErrorCode> File::getc() {
  if (!opened) {
    return Result<char, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
  }

  // Read 1 byte at current position
  // Note: We don't track position in File class, server handles it
  FilesystemClient client(fs_pid);
  auto result = client.read(FileHandleId(handle), 0, 1);
  if (result.is_err()) {
    return Result<char, ErrorCode>::err(result.error());
  }

  uintptr_t bytes_read = result.value();
  if (bytes_read == 0) {
    // EOF
    return Result<char, ErrorCode>::err(FILESYSTEM__IO_ERROR);
  }

  // Read data from comm page
  PageAddr comm = ou_get_comm_page();
  MPackReader reader(comm.as<char>(), OT_PAGE_SIZE);

  StringView bin;
  if (!reader.read_bin(bin)) {
    return Result<char, ErrorCode>::err(FILESYSTEM__IO_ERROR);
  }

  if (bin.len == 0) {
    return Result<char, ErrorCode>::err(FILESYSTEM__IO_ERROR);
  }

  return Result<char, ErrorCode>::ok(bin.ptr[0]);
}

ErrorCode File::write(const ou::string &data) {
  if (!opened) {
    return FILESYSTEM__INVALID_HANDLE;
  }

  // Convert string to vector for IPC call
  ou::vector<uint8_t> vec;
  for (size_t i = 0; i < data.length(); i++) {
    vec.push_back(static_cast<uint8_t>(data[i]));
  }

  FilesystemClient client(fs_pid);
  auto result = client.write(FileHandleId(handle), write_offset_, vec);
  if (result.is_err()) {
    return result.error();
  }

  // Advance write position
  write_offset_ += result.value();

  return NONE;
}

ErrorCode File::write(const char *data) { return write(ou::string(data)); }

ErrorCode File::read_all(ou::string &out_data) {
  if (!opened) {
    return FILESYSTEM__INVALID_HANDLE;
  }

  out_data.clear();
  FilesystemClient client(fs_pid);

  // Read file in chunks until EOF
  uintptr_t offset = 0;
  const uintptr_t chunk_size = 4000; // Leave room for msgpack overhead in 4KB page

  while (true) {
    auto result = client.read(FileHandleId(handle), offset, chunk_size);
    if (result.is_err()) {
      return result.error();
    }

    uintptr_t bytes_read = result.value();
    if (bytes_read == 0) {
      // EOF reached
      break;
    }

    // Read data from comm page
    PageAddr comm = ou_get_comm_page();
    MPackReader reader(comm.as<char>(), OT_PAGE_SIZE);

    StringView bin;
    if (!reader.read_bin(bin)) {
      return FILESYSTEM__IO_ERROR;
    }

    // Append entire chunk at once (much more efficient than char-by-char)
    out_data.append(bin.ptr, bin.len);

    offset += bytes_read;

    // If we read less than requested, we've reached EOF
    if (bytes_read < chunk_size) {
      break;
    }
  }

  return NONE;
}

ErrorCode File::write_all(const ou::string &data) {
  if (!opened) {
    return FILESYSTEM__INVALID_HANDLE;
  }

  FilesystemClient client(fs_pid);

  // Write file in chunks
  uintptr_t offset = 0;
  const uintptr_t chunk_size = 4000; // Leave room for msgpack overhead

  while (offset < data.length()) {
    size_t remaining = data.length() - offset;
    size_t to_write = (remaining < chunk_size) ? remaining : chunk_size;

    // Prepare data chunk
    ou::vector<uint8_t> chunk;
    for (size_t i = 0; i < to_write; i++) {
      chunk.push_back(static_cast<uint8_t>(data[offset + i]));
    }

    // Write chunk
    auto result = client.write(FileHandleId(handle), offset, chunk);
    if (result.is_err()) {
      return result.error();
    }

    uintptr_t bytes_written = result.value();
    if (bytes_written != to_write) {
      return FILESYSTEM__IO_ERROR;
    }

    offset += bytes_written;
  }

  return NONE;
}

#endif // !OT_POSIX

} // namespace ou
