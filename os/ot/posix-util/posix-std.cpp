// posix-std.cpp - POSIX implementations of OT functions
#include "ot/common.h"
#include <stdio.h>

// Provide POSIX implementation of oputchar
extern "C" int oputchar(char c) {
  return putchar(c) != EOF ? 1 : 0;
}

// Provide POSIX implementation of oputsn
extern "C" int oputsn(const char *str, int n) {
  for (int i = 0; i < n; i++) {
    if (!oputchar(str[i])) {
      return 0;
    }
  }
  return 1;
}
