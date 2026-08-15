// vec.hpp — malloc-backed growable vector and byte/string builder.
#pragma once
#include "common.hpp"
#include <cstdarg>

namespace ot {

template <typename T>
struct Vec {
  T* data;
  u32 len;
  u32 cap;

  Vec() : data(nullptr), len(0), cap(0) {}
  ~Vec() { free(static_cast<void*>(data)); }

  Vec(const Vec&) = delete;
  Vec& operator=(const Vec&) = delete;
  Vec(Vec&& o) : data(o.data), len(o.len), cap(o.cap) {
    o.data = nullptr;
    o.len = 0;
    o.cap = 0;
  }
  Vec& operator=(Vec&& o) {
    if (this != &o) {
      free(static_cast<void*>(data));
      data = o.data;
      len = o.len;
      cap = o.cap;
      o.data = nullptr;
      o.len = 0;
      o.cap = 0;
    }
    return *this;
  }

  void reserve(u32 n) {
    if (n <= cap) return;
    u32 ncap = cap ? cap : 8;
    while (ncap < n) ncap *= 2;
    T* nd = (T*)realloc(static_cast<void*>(data), (size_t)ncap * sizeof(T));
    if (!nd) ot_fatal("Vec: out of memory");
    data = nd;
    cap = ncap;
  }

  void push(T v) {
    reserve(len + 1);
    data[len++] = v;
  }

  T pop() {
    OT_ASSERT(len > 0);
    return data[--len];
  }

  T& operator[](u32 i) {
    OT_ASSERT(i < len);
    return data[i];
  }

  void clear() { len = 0; }
};

struct Buf {
  char* data;
  u32 len;
  u32 cap;

  Buf() : data(nullptr), len(0), cap(0) {}
  ~Buf() { free(data); }
  Buf(const Buf&) = delete;
  Buf& operator=(const Buf&) = delete;
  Buf(Buf&& o) : data(o.data), len(o.len), cap(o.cap) {
    o.data = nullptr;
    o.len = 0;
    o.cap = 0;
  }
  Buf& operator=(Buf&& o) {
    if (this != &o) {
      free(data);
      data = o.data;
      len = o.len;
      cap = o.cap;
      o.data = nullptr;
      o.len = 0;
      o.cap = 0;
    }
    return *this;
  }

  void reserve(u32 n) {
    if (n <= cap) return;
    u32 ncap = cap ? cap : 16;
    while (ncap < n) ncap *= 2;
    char* nd = (char*)realloc(data, ncap);
    if (!nd) ot_fatal("Buf: out of memory");
    data = nd;
    cap = ncap;
  }

  void push(char c) {
    reserve(len + 1);
    data[len++] = c;
  }

  void append(const char* s, u32 n) {
    if (!n) return;
    reserve(len + n);
    memcpy(data + len, s, n);
    len += n;
  }

  void appendCstr(const char* s) { append(s, (u32)strlen(s)); }

  void printf(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int need = vsnprintf(nullptr, 0, fmt, ap);
    va_end(ap);
    if (need > 0) {
      reserve(len + (u32)need + 1);
      vsnprintf(data + len, (size_t)need + 1, fmt, ap2);
      len += (u32)need;
    }
    va_end(ap2);
  }

  void clear() { len = 0; }
};

}  // namespace ot
