#include "utf8.h"

int ot_utf8_valid(const unsigned char* p, size_t n) {
  for (size_t i = 0; i < n;) {
    unsigned c = p[i++];
    if (c < 128) continue;
    unsigned count, min, v;
    if (c >= 0xc2 && c <= 0xdf) {
      count = 1;
      min = 0x80;
      v = c & 31;
    } else if (c >= 0xe0 && c <= 0xef) {
      count = 2;
      min = 0x800;
      v = c & 15;
    } else if (c >= 0xf0 && c <= 0xf4) {
      count = 3;
      min = 0x10000;
      v = c & 7;
    } else return 0;
    if (n - i < count) return 0;
    while (count--) {
      c = p[i++];
      if ((c & 0xc0) != 0x80) return 0;
      v = (v << 6) | (c & 63);
    }
    if (v < min || v > 0x10ffff || (v >= 0xd800 && v <= 0xdfff)) return 0;
  }
  return 1;
}
