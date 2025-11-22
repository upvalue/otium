#ifndef OT_LIB_ARRAY_VIEW_HPP
#define OT_LIB_ARRAY_VIEW_HPP

#include "ot/common.h"

// Non-owning view of an array (similar to StringView but for typed arrays)
template <typename T> struct ArrayView {
  const T *ptr;
  size_t len;

  // Constructors
  ArrayView(const T *ptr, size_t len) : ptr(ptr), len(len) {}
  ArrayView() : ptr(nullptr), len(0) {}

  // Check if empty
  bool empty() const { return len == 0; }

  // Get size
  size_t size() const { return len; }

  // Element access (unchecked)
  const T &operator[](size_t index) const { return ptr[index]; }

  // Iterator support for range-based for loops
  const T *begin() const { return ptr; }
  const T *end() const { return ptr + len; }

  // Get pointer to underlying data
  const T *data() const { return ptr; }

  // Bounds-checked access
  bool get(size_t index, T &out) const {
    if (index >= len) {
      return false;
    }
    out = ptr[index];
    return true;
  }

  // Print to oprintf (for debugging - T must be printable as size_t)
  void print() const {
    oprintf("[");
    for (size_t i = 0; i < len; i++) {
      if (i > 0)
        oprintf(", ");
      // Print as size_t - works for most integer types
      oprintf("%zu", (size_t)ptr[i]);
    }
    oprintf("]");
  }

  // Copy to a buffer
  bool copy_to(T *buffer, size_t bufsize) const {
    if (bufsize < len) {
      return false; // Not enough space
    }
    for (size_t i = 0; i < len; i++) {
      buffer[i] = ptr[i];
    }
    return true;
  }

  // Comparison
  bool equals(const ArrayView<T> &other) const {
    if (len != other.len)
      return false;
    for (size_t i = 0; i < len; i++) {
      if (ptr[i] != other.ptr[i])
        return false;
    }
    return true;
  }
};

#endif // OT_LIB_ARRAY_VIEW_HPP
