#include "ot/common.h"

#ifndef OT_POSIX
int oputsn(const char *str, int n) {
  for (int i = 0; i < n; i++) {
    oputchar(str[i]);
  }
  return 1;
}
#endif
