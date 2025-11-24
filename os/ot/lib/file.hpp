#ifndef OT_USER_FILE_HPP
#define OT_USER_FILE_HPP

#include "ot/common.h"
#include "ot/lib/error-codes.hpp"
#include "ot/lib/result.hpp"
#include "ot/user/string.hpp"
#include "ot/user/vector.hpp"

#ifdef OT_POSIX
#include <stdio.h>
#else
#include "ot/lib/typed-int.hpp"
#endif

namespace ou {

enum class FileMode {
  READ,
  WRITE,
  APPEND,
};

typedef void (*LineCallback)(const ou::string &line);

struct File {
  File(const char *path, FileMode mode = FileMode::READ);
  ~File();

  ou::string path_;
  ou::string buffer;
  FileMode mode_;
  bool opened;

#ifdef OT_POSIX
  FILE *file_handle;
#else
  Pid fs_pid;            // Filesystem server PID
  uintptr_t handle;      // IPC file handle (FileHandleId)
#endif

  ErrorCode open();

  /** Calls callback for each line in the file. If EOF is reached, this is treated as a line. */
  ErrorCode forEachLine(LineCallback callback);
  Result<char, ErrorCode> getc();

  ErrorCode write(const ou::string &data);
  ErrorCode write(const char *data);

  // Utility methods
  ErrorCode read_all(ou::string &out_data);
  ErrorCode write_all(const ou::string &data);
};
} // namespace ou

#endif // OT_USER_FILE_HPP