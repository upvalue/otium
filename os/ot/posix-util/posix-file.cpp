#include "ot/user/file.hpp"

namespace ou {

#ifdef OT_POSIX
File::File(const char *path) : path_(path), opened(false) {}

File::~File() {}

FileErr File::open() {
  file_handle = fopen(path_.c_str(), "r");
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

#endif

} // namespace ou
