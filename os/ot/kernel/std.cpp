#include "ot/common.h"

int oputsn(const char *str, int n) {
  for (int i = 0; i < n; i++) {
    oputchar(str[i]);
  }
  return 1;
}
