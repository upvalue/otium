// string.cpp - implementation of generic string class

#include "ot/user/string.hpp"

namespace ou {

//
// STRING IMPLEMENTATION
//

void string::ensure_capacity(size_t new_cap) {
  if (new_cap <= cap_)
    return;
  size_t alloc_cap = cap_ == 0 ? 16 : cap_;
  while (alloc_cap < new_cap)
    alloc_cap *= 2;
  char *new_data = (char *)ou_realloc(data_, alloc_cap);
  data_ = new_data;
  cap_ = alloc_cap;
}

string::string() : data_(nullptr), len_(0), cap_(0) {}

string::string(const char *s) : data_(nullptr), len_(0), cap_(0) {
  if (s) {
    len_ = strlen(s);
    ensure_capacity(len_ + 1);
    memcpy(data_, s, len_);
    data_[len_] = '\0';
  }
}

string::string(const char *s, size_t n) : data_(nullptr), len_(0), cap_(0) {
  if (s && n > 0) {
    len_ = n;
    ensure_capacity(len_ + 1);
    memcpy(data_, s, len_);
    data_[len_] = '\0';
  }
}

string::string(const string &other) : data_(nullptr), len_(0), cap_(0) {
  if (other.len_ > 0) {
    len_ = other.len_;
    ensure_capacity(len_ + 1);
    memcpy(data_, other.data_, len_);
    data_[len_] = '\0';
  }
}

string::string(string &&other) noexcept
    : data_(other.data_), len_(other.len_), cap_(other.cap_) {
  other.data_ = nullptr;
  other.len_ = 0;
  other.cap_ = 0;
}

string::~string() {
  if (data_)
    ou_free(data_);
}

string &string::operator=(const string &other) {
  if (this != &other) {
    clear();
    if (other.len_ > 0) {
      len_ = other.len_;
      ensure_capacity(len_ + 1);
      memcpy(data_, other.data_, len_);
      data_[len_] = '\0';
    }
  }
  return *this;
}

string &string::operator=(string &&other) noexcept {
  if (this != &other) {
    if (data_)
      ou_free(data_);
    data_ = other.data_;
    len_ = other.len_;
    cap_ = other.cap_;
    other.data_ = nullptr;
    other.len_ = 0;
    other.cap_ = 0;
  }
  return *this;
}

string &string::operator=(const char *s) {
  clear();
  if (s) {
    len_ = strlen(s);
    ensure_capacity(len_ + 1);
    memcpy(data_, s, len_);
    data_[len_] = '\0';
  }
  return *this;
}

void string::clear() {
  len_ = 0;
  if (data_)
    data_[0] = '\0';
}

void string::reserve(size_t new_cap) { ensure_capacity(new_cap); }

void string::append(const char *s, size_t n) {
  if (s && n > 0) {
    ensure_capacity(len_ + n + 1);
    memcpy(data_ + len_, s, n);
    len_ += n;
    data_[len_] = '\0';
  }
}

void string::append(const char *s) {
  if (s)
    append(s, strlen(s));
}

void string::append(const string &s) { append(s.data_, s.len_); }

void string::push_back(char c) {
  ensure_capacity(len_ + 2);
  data_[len_++] = c;
  data_[len_] = '\0';
}

string &string::operator+=(const char *s) {
  append(s);
  return *this;
}

string &string::operator+=(const string &s) {
  append(s);
  return *this;
}

string &string::operator+=(char c) {
  push_back(c);
  return *this;
}

int string::compare(const char *s) const {
  if (!data_ && !s)
    return 0;
  if (!data_)
    return -1;
  if (!s)
    return 1;
  return strcmp(data_, s);
}

int string::compare(const string &s) const { return compare(s.data_); }

int string::compare(const string_view &s) const {
  if (!data_ && !s.data_)
    return 0;
  if (!data_)
    return -1;
  if (!s.data_)
    return 1;
  size_t minlen = len_ < s.len_ ? len_ : s.len_;
  int cmp = memcmp(data_, s.data_, minlen);
  if (cmp == 0)
    return len_ < s.len_ ? -1 : (len_ > s.len_ ? 1 : 0);
  return cmp;
}

string string::substr(size_t pos, size_t len) const {
  if (pos > len_)
    pos = len_;
  if (pos + len > len_)
    len = len_ - pos;
  return string(data_ + pos, len);
}

} // namespace ou
