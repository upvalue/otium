#include "ot/common.h"

#if !defined(OT_ARCH_WASM) && !defined(OT_POSIX)
int oputsn(const char *str, int n) {
  for (int i = 0; i < n; i++) {
    oputchar(str[i]);
  }
  return 1;
}
#endif // !OT_ARCH_WASM && !OT_POSIX
