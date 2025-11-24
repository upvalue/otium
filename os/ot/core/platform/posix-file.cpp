#include "ot/lib/file.hpp"
#include <errno.h>

namespace ou {

#ifdef OT_POSIX
File::File(const char *path, FileMode mode)
    : path_(path), mode_(mode), opened(false), file_handle(nullptr) {}

File::~File() {
  if (opened && file_handle) {
    fclose(file_handle);
  }
}

ErrorCode File::open() {
  const char *mode_str;
  switch (mode_) {
  case FileMode::READ:
    mode_str = "r";
    break;
  case FileMode::WRITE:
    mode_str = "w";
    break;
  case FileMode::APPEND:
    mode_str = "a";
    break;
  }

  file_handle = fopen(path_.c_str(), mode_str);
  if (!file_handle) {
    // Map errno to appropriate ErrorCode
    if (errno == ENOENT) {
      return FILESYSTEM__FILE_NOT_FOUND;
    }
    return FILESYSTEM__IO_ERROR;
  }
  opened = true;
  return NONE;
}

Result<char, ErrorCode> File::getc() {
  if (!opened) {
    return Result<char, ErrorCode>::err(FILESYSTEM__INVALID_HANDLE);
  }
  int c = fgetc(file_handle);
  if (c == EOF) {
    return Result<char, ErrorCode>::err(FILESYSTEM__IO_ERROR);
  }
  return Result<char, ErrorCode>::ok(static_cast<char>(c));
}

ErrorCode File::write(const ou::string &data) {
  if (!opened) {
    return FILESYSTEM__INVALID_HANDLE;
  }
  size_t written = fwrite(data.c_str(), 1, data.length(), file_handle);
  if (written != data.length()) {
    return FILESYSTEM__IO_ERROR;
  }
  return NONE;
}

ErrorCode File::write(const char *data) {
  if (!opened) {
    return FILESYSTEM__INVALID_HANDLE;
  }
  size_t len = strlen(data);
  size_t written = fwrite(data, 1, len, file_handle);
  if (written != len) {
    return FILESYSTEM__IO_ERROR;
  }
  return NONE;
}

ErrorCode File::read_all(ou::string &out_data) {
  if (!opened) {
    return FILESYSTEM__INVALID_HANDLE;
  }

  // Get file size
  long current_pos = ftell(file_handle);
  if (fseek(file_handle, 0, SEEK_END) != 0) {
    return FILESYSTEM__IO_ERROR;
  }
  long size = ftell(file_handle);
  if (size < 0) {
    return FILESYSTEM__IO_ERROR;
  }
  if (fseek(file_handle, 0, SEEK_SET) != 0) {
    return FILESYSTEM__IO_ERROR;
  }

  // Read entire file
  out_data.clear();
  out_data.reserve(size + 1); // Reserve space for null terminator

  // Read in chunks and append
  char buffer[4096];
  size_t total_read = 0;
  while (total_read < static_cast<size_t>(size)) {
    size_t to_read = (size - total_read > 4096) ? 4096 : (size - total_read);
    size_t bytes_read = fread(buffer, 1, to_read, file_handle);
    if (bytes_read == 0) {
      break;
    }
    out_data.append(buffer, bytes_read);
    total_read += bytes_read;
  }

  if (total_read != static_cast<size_t>(size)) {
    return FILESYSTEM__IO_ERROR;
  }

  // Restore original position if it was valid
  if (current_pos >= 0) {
    fseek(file_handle, current_pos, SEEK_SET);
  }

  return NONE;
}

ErrorCode File::write_all(const ou::string &data) {
  if (!opened) {
    return FILESYSTEM__INVALID_HANDLE;
  }

  // Seek to beginning
  if (fseek(file_handle, 0, SEEK_SET) != 0) {
    return FILESYSTEM__IO_ERROR;
  }

  // Write all data
  size_t written = fwrite(data.c_str(), 1, data.length(), file_handle);
  if (written != data.length()) {
    return FILESYSTEM__IO_ERROR;
  }

  // Flush to ensure data is written
  if (fflush(file_handle) != 0) {
    return FILESYSTEM__IO_ERROR;
  }

  return NONE;
}

#endif

} // namespace ou
