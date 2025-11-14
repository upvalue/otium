#include "ot/lib/file.hpp"

namespace ou {

FileErr File::forEachLine(LineCallback callback) {
  if (!opened) {
    return FileErr::NOT_OPENED;
  }
  buffer.clear();
  while (true) {
    auto res = getc();
    if (res.is_err()) {
      // EOF reached, process final line if any
      break;
    }
    char c = res.value();
    if (c == '\n') {
      callback(buffer);
      buffer.clear();
    } else if (c == '\r') {
      continue;
    } else {
      buffer.push_back(c);
    }
  }
  if (buffer.length() > 0) {
    callback(buffer);
  }
  return FileErr::NONE;
}
} // namespace ou