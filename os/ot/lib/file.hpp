#ifndef OT_USER_FILE_HPP
#define OT_USER_FILE_HPP

#include "ot/common.h"
#include "ot/lib/result.hpp"
#include "ot/user/string.hpp"
#include "ot/user/vector.hpp"

#ifdef OT_POSIX
#include <stdio.h>
#endif

namespace ou {

enum class FileMode {
  READ,
  WRITE,
  APPEND,
};

enum class FileErr {
  NONE,
  OPEN_FAILED,
  NOT_OPENED,
  READ_FAILED,
  WRITE_FAILED,
};

typedef void (*LineCallback)(const ou::string &line);

struct File {
  File(const char *path, FileMode mode = FileMode::READ);
  ~File();

  ou::string path_;
  ou::string buffer;
  FileMode mode_;
  bool opened;
  FileErr open();

  /** Calls callback for each line in the file. If EOF is reached, this is treated as a line. */
  FileErr forEachLine(LineCallback callback);
  Result<char, FileErr> getc();

  FileErr write(const ou::string &data);
  FileErr write(const char *data);

#ifdef OT_POSIX
  FILE *file_handle;
#endif
};
} // namespace ou

#endif // OT_USER_FILE_HPP