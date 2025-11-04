#ifndef OT_USER_FILE_HPP
#define OT_USER_FILE_HPP

#include "ot/common.h"
#include "ot/shared/result.hpp"
#include "ot/user/string.hpp"
#include "ot/user/vector.hpp"

#ifdef OT_POSIX
#include <stdio.h>
#endif

namespace ou {

enum class FileErr {
  NONE,
  OPEN_FAILED,
  NOT_OPENED,
  READ_FAILED,
};

typedef void (*LineCallback)(const ou::string &line);

struct File {
  File(const char *path);
  ~File();

  ou::string path_;
  ou::string buffer;
  bool opened;
  FileErr open();

  /** Calls callback for each line in the file. If EOF is reached, this is treated as a line. */
  FileErr forEachLine(LineCallback callback);
  Result<char, FileErr> getc();

#ifdef OT_POSIX
  FILE *file_handle;
#endif
};
} // namespace ou

#endif // OT_USER_FILE_HPP