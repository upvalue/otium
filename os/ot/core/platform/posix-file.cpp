#include "ot/lib/file.hpp"

namespace ou {

#ifdef OT_POSIX
File::File(const char *path, FileMode mode) : path_(path), mode_(mode), opened(false) {}

File::~File() {
  if (opened && file_handle) {
    fclose(file_handle);
  }
}

FileErr File::open() {
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
    return FileErr::OPEN_FAILED;
  }
  opened = true;
  return FileErr::NONE;
}

Result<char, FileErr> File::getc() {
  if (!opened) {
    return Result<char, FileErr>::err(FileErr::NOT_OPENED);
  }
  int c = fgetc(file_handle);
  if (c == EOF) {
    return Result<char, FileErr>::err(FileErr::READ_FAILED);
  }
  return Result<char, FileErr>::ok(c);
}

FileErr File::write(const ou::string &data) {
  if (!opened) {
    return FileErr::NOT_OPENED;
  }
  size_t written = fwrite(data.c_str(), 1, data.length(), file_handle);
  if (written != data.length()) {
    return FileErr::WRITE_FAILED;
  }
  return FileErr::NONE;
}

FileErr File::write(const char *data) {
  if (!opened) {
    return FileErr::NOT_OPENED;
  }
  size_t len = strlen(data);
  size_t written = fwrite(data, 1, len, file_handle);
  if (written != len) {
    return FileErr::WRITE_FAILED;
  }
  return FileErr::NONE;
}

#endif

} // namespace ou
