// string.hpp - generic string and string_view for otium user space
// Uses only ou_malloc/ou_free/ou_realloc from memory allocator

#ifndef OT_USER_STRING_HPP
#define OT_USER_STRING_HPP

#include "ot/common.h"

// Forward declarations for memory allocation functions
extern "C" {
void *ou_malloc(size_t size);
void ou_free(void *ptr);
void *ou_realloc(void *ptr, size_t size);
}

// Placement new operator (avoids needing <new>)
// In POSIX environments, use standard library version
#ifdef OT_POSIX
#include <new>
#else
inline void *operator new(size_t, void *p) noexcept { return p; }
inline void operator delete(void *, void *) noexcept {}
#endif

namespace ou {

// Memory allocation helpers using ou_malloc/ou_free
template <typename T, typename... Args> T *ou_new(Args &&...args) {
  void *mem = ou_malloc(sizeof(T));
  if (!mem)
    return nullptr;
  return new (mem) T(static_cast<Args &&>(args)...);
}

template <typename T> void ou_delete(T *ptr) {
  if (ptr) {
    ptr->~T();
    ou_free(ptr);
  }
}

// Forward declaration
struct string_view;

/**
 * Lightweight string class using ou_malloc/ou_free
 */
class string {
private:
  char *data_;
  size_t len_;
  size_t cap_;

public:
  string();
  string(const char *s);
  string(const char *s, size_t n);
  string(const string &other);
  string(string &&other) noexcept;
  ~string();

  string &operator=(const string &other);
  string &operator=(string &&other) noexcept;
  string &operator=(const char *s);

  size_t length() const { return len_; }
  size_t size() const { return len_; }
  size_t capacity() const { return cap_; }
  const char *c_str() const { return data_ ? data_ : ""; }
  const char *data() const { return data_ ? data_ : ""; }
  bool empty() const { return len_ == 0; }

  char operator[](size_t i) const { return data_[i]; }
  char &operator[](size_t i) { return data_[i]; }
  char at(size_t i) const { return data_[i]; }

  void clear();
  void reserve(size_t new_cap);
  void append(const char *s, size_t n);
  void append(const char *s);
  void append(const string &s);
  void push_back(char c);
  void insert(size_t pos, size_t count, char c);
  void erase(size_t pos, size_t len);
  void erase(size_t pos);

  string &operator+=(const char *s);
  string &operator+=(const string &s);
  string &operator+=(char c);

  int compare(const char *s) const;
  int compare(const string &s) const;
  int compare(const string_view &s) const;

  string substr(size_t pos, size_t len) const;
  string substr(size_t pos) const;
  void ensure_capacity(size_t new_cap);
};

/**
 * Lightweight string view (non-owning reference)
 */
struct string_view {
  const char *data_;
  size_t len_;

  string_view() : data_(nullptr), len_(0) {}
  string_view(const char *s) : data_(s), len_(s ? strlen(s) : 0) {}
  string_view(const char *s, size_t n) : data_(s), len_(n) {}
  string_view(const string &s) : data_(s.c_str()), len_(s.length()) {}

  const char *data() const { return data_ ? data_ : ""; }
  size_t length() const { return len_; }
  size_t size() const { return len_; }
  bool empty() const { return len_ == 0; }

  char operator[](size_t i) const { return data_[i]; }

  string_view substr(size_t pos, size_t len) const {
    if (pos > len_)
      pos = len_;
    if (pos + len > len_)
      len = len_ - pos;
    return string_view(data_ + pos, len);
  }

  int compare(const char *s) const {
    size_t slen = strlen(s);
    int cmp = memcmp(data_, s, len_ < slen ? len_ : slen);
    if (cmp == 0)
      return len_ < slen ? -1 : (len_ > slen ? 1 : 0);
    return cmp;
  }

  int compare(const string_view &other) const {
    size_t minlen = len_ < other.len_ ? len_ : other.len_;
    int cmp = memcmp(data_, other.data_, minlen);
    if (cmp == 0)
      return len_ < other.len_ ? -1 : (len_ > other.len_ ? 1 : 0);
    return cmp;
  }
};

} // namespace ou

#endif
