// regstr.hpp - encode/decode pure-ASCII strings in two uint32_t registers
#ifndef OT_SHARED_REGSTR_HPP
#define OT_SHARED_REGSTR_HPP

#include "ot/common.h"

#define REGSTR_MAX_LEN 8

struct regstr {
  uint32_t a, b;

  // Constructor from two uint32_t values
  regstr(uint32_t _a, uint32_t _b) : a(_a), b(_b) {}

  // Constructor from string - encodes up to 8 ASCII characters
  // If string exceeds REGSTR_MAX_LEN, stores "err" instead
  regstr(const char *str) {
    size_t len = strlen(str);

    // Check for embedded nulls by verifying actual length matches expected
    for (size_t i = 0; i < len; i++) {
      if (str[i] == '\0') {
        // Embedded null detected, use "err"
        *this = regstr("err");
        return;
      }
    }

    if (len > REGSTR_MAX_LEN) {
      // String too long, use "err"
      *this = regstr("err");
      return;
    }

    // Pack characters into a and b
    a = 0;
    b = 0;

    for (size_t i = 0; i < len && i < 4; i++) {
      a |= ((uint32_t)(uint8_t)str[i]) << (i * 8);
    }

    for (size_t i = 4; i < len && i < 8; i++) {
      b |= ((uint32_t)(uint8_t)str[i]) << ((i - 4) * 8);
    }
  }

  // Extract string to buffer (must be char[8])
  // Returns length of extracted string, or 0 on error
  size_t extract(char buf[8]) const {
    size_t len = 0;

    // Extract from a (first 4 bytes)
    for (int i = 0; i < 4; i++) {
      uint8_t c = (a >> (i * 8)) & 0xFF;
      if (c == 0)
        break;
      buf[len++] = c;
    }

    // Extract from b (next 4 bytes)
    for (int i = 0; i < 4; i++) {
      uint8_t c = (b >> (i * 8)) & 0xFF;
      if (c == 0)
        break;
      buf[len++] = c;
    }

    // Null-terminate the remaining buffer
    for (size_t i = len; i < 8; i++) {
      buf[i] = '\0';
    }

    return len;
  }
};

#endif
