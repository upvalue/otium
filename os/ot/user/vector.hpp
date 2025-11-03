// vector.hpp - generic dynamic array template for otium user space
// Uses only ou_malloc/ou_free from memory allocator

#ifndef OT_USER_VECTOR_HPP
#define OT_USER_VECTOR_HPP

#include "ot/common.h"
#include "ot/user/string.hpp"

namespace ou {

/**
 * Dynamic array template using ou_malloc/ou_free
 */
template <typename T> class vector {
private:
  T *data_;
  size_t size_;
  size_t cap_;

  void ensure_capacity(size_t new_cap) {
    if (new_cap <= cap_)
      return;
    size_t alloc_cap = cap_ == 0 ? 8 : cap_;
    while (alloc_cap < new_cap)
      alloc_cap *= 2;
    T *new_data = (T *)ou_malloc(alloc_cap * sizeof(T));
    if (data_) {
      for (size_t i = 0; i < size_; i++) {
        new (&new_data[i]) T(static_cast<T &&>(data_[i]));
        data_[i].~T();
      }
      ou_free(data_);
    }
    data_ = new_data;
    cap_ = alloc_cap;
  }

public:
  vector() : data_(nullptr), size_(0), cap_(0) {}

  ~vector() {
    if (data_) {
      for (size_t i = 0; i < size_; i++) {
        data_[i].~T();
      }
      ou_free(data_);
    }
  }

  vector(const vector &) = delete;
  vector &operator=(const vector &) = delete;

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  T &operator[](size_t i) { return data_[i]; }
  const T &operator[](size_t i) const { return data_[i]; }

  T &back() { return data_[size_ - 1]; }
  const T &back() const { return data_[size_ - 1]; }

  void push_back(const T &val) {
    ensure_capacity(size_ + 1);
    new (&data_[size_]) T(val);
    size_++;
  }

  void push_back(T &&val) {
    ensure_capacity(size_ + 1);
    new (&data_[size_]) T(static_cast<T &&>(val));
    size_++;
  }

  void pop_back() {
    if (size_ > 0) {
      size_--;
      data_[size_].~T();
    }
  }

  void clear() {
    for (size_t i = 0; i < size_; i++) {
      data_[i].~T();
    }
    size_ = 0;
  }

  // Iterator support (minimal)
  T *begin() { return data_; }
  T *end() { return data_ + size_; }
  const T *begin() const { return data_; }
  const T *end() const { return data_ + size_; }
};

} // namespace ou

#endif
