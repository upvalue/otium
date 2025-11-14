#include "ot/common.h"
#include <emscripten.h>

EM_JS(uintptr_t, _o_time_get, (), { return Module.time_get(); });

EM_JS(void, _o_puts, (char *str), { Module.print2(UTF8ToString(str), false); });
uint64_t o_time_get(void) { return _o_time_get(); }

int ou_io_puts(char *str, int size) {
  for (int i = 0; i < size; i++) {
    oputchar(str[i]);
  }
  return 1;
}

int oputsn(const char *str, int size) {
  _o_puts((char *)str);
  return 1;
}