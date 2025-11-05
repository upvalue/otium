// posix-std.cpp - POSIX implementations of OT functions
#include "ot/common.h"
#include <chrono>
#include <stdio.h>

// Provide POSIX implementation of oputchar
extern "C" int oputchar(char c) { return putchar(c) != EOF ? 1 : 0; }

// Provide POSIX implementation of oputsn
extern "C" int oputsn(const char *str, int n) {
  for (int i = 0; i < n; i++) {
    if (!oputchar(str[i])) {
      return 0;
    }
  }
  return 1;
}

uint64_t o_time_get() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}
