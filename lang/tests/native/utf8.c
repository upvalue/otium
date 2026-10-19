#include "utf8.h"
#include <stdio.h>

int main(void) {
  static const struct {
    const char* bytes;
    size_t size;
    int valid;
  } cases[] = {
      {"", 0, 1},
      {"hello", 5, 1},
      {"a\0b", 3, 1},
      {"\xc2\x80", 2, 1},
      {"\xdf\xbf", 2, 1},
      {"\xe0\xa0\x80", 3, 1},
      {"\xed\x9f\xbf", 3, 1},
      {"\xee\x80\x80", 3, 1},
      {"\xef\xbf\xbf", 3, 1},
      {"\xf0\x90\x80\x80", 4, 1},
      {"\xf4\x8f\xbf\xbf", 4, 1},
      {"\x80", 1, 0},
      {"\xc0\xaf", 2, 0},
      {"\xc1\xbf", 2, 0},
      {"\xe0\x9f\xbf", 3, 0},
      {"\xed\xa0\x80", 3, 0},
      {"\xed\xbf\xbf", 3, 0},
      {"\xf0\x8f\xbf\xbf", 4, 0},
      {"\xf4\x90\x80\x80", 4, 0},
      {"\xf5\x80\x80\x80", 4, 0},
      {"\xff", 1, 0},
      {"\xc2x", 2, 0},
      {"\xc2\x80", 1, 0},
      {"\xe2\x82\xac", 2, 0},
      {"\xf0\x9f\x98\x80", 3, 0},
      {"\xc2\0", 2, 0},
  };
  for (size_t i = 0; i < sizeof cases / sizeof cases[0]; ++i) {
    if (ot_utf8_valid((const unsigned char*)cases[i].bytes, cases[i].size) != cases[i].valid) {
      fprintf(stderr, "UTF-8 case %zu failed\n", i);
      return 1;
    }
  }
  return 0;
}
