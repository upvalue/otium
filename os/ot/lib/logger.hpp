#pragma once

#include "ot/common.h"
#include <stdarg.h>

class Logger {
private:
  const char* prefix;

public:
  explicit Logger(const char* prefix) : prefix(prefix) {}

  void log(const char* format, ...) __attribute__((format(printf, 2, 3))) {
    char buffer[256];  // Temporary buffer for formatting

    va_list args;
    va_start(args, format);
    ovsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    oprintf("[%s] %s\n", prefix, buffer);
  }
};
