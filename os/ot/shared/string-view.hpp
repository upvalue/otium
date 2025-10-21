#ifndef OT_SHARED_STRING_VIEW_HPP
#define OT_SHARED_STRING_VIEW_HPP

#include "ot/common.h"

// Non-owning view of a string (not necessarily null-terminated)
struct StringView {
  const char* ptr;
  size_t len;

  // Check if empty
  bool empty() const {
    return len == 0;
  }

  // Compare with null-terminated C string
  bool equals(const char* s) const {
    if (!s) return false;

    size_t s_len = 0;
    while (s[s_len]) s_len++;

    if (len != s_len) return false;

    for (size_t i = 0; i < len; i++) {
      if (ptr[i] != s[i]) return false;
    }
    return true;
  }

  // Print to oprintf (handles non-null-terminated strings)
  void print() const {
    for (size_t i = 0; i < len; i++) {
      oputchar(ptr[i]);
    }
  }

  // Copy to a buffer and null-terminate
  bool copy_to(char* buffer, size_t bufsize) const {
    if (bufsize == 0 || len >= bufsize) {
      return false;  // Not enough space for string + null
    }
    for (size_t i = 0; i < len; i++) {
      buffer[i] = ptr[i];
    }
    buffer[len] = '\0';
    return true;
  }

  // Get character at index (unchecked)
  char operator[](size_t index) const {
    return ptr[index];
  }
};

#endif  // OT_SHARED_STRING_VIEW_HPP
