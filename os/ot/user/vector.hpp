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

  // Move constructor
  vector(vector &&other) noexcept : data_(other.data_), size_(other.size_), cap_(other.cap_) {
    other.data_ = nullptr;
    other.size_ = 0;
    other.cap_ = 0;
  }

  // Copy is deleted
  vector(const vector &) = delete;
  vector &operator=(const vector &) = delete;

  size_t size() const { return size_; }
  bool empty() const { return size_ == 0; }

  T *data() { return data_; }
  const T *data() const { return data_; }

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

  void resize(size_t new_size, const T& default_value = T()) {
    if (new_size < size_) {
      // Shrink: destroy extra elements
      for (size_t i = new_size; i < size_; i++) {
        data_[i].~T();
      }
      size_ = new_size;
    } else if (new_size > size_) {
      // Grow: add default elements
      ensure_capacity(new_size);
      for (size_t i = size_; i < new_size; i++) {
        new (&data_[i]) T(default_value);
      }
      size_ = new_size;
    }
  }

  void clear() {
    for (size_t i = 0; i < size_; i++) {
      data_[i].~T();
    }
    size_ = 0;
  }

  void insert(size_t pos, const T &val) {
    if (pos > size_)
      pos = size_;
    ensure_capacity(size_ + 1);
    // Move existing elements to make room
    for (size_t i = size_; i > pos; i--) {
      new (&data_[i]) T(static_cast<T &&>(data_[i - 1]));
      data_[i - 1].~T();
    }
    // Insert new element
    new (&data_[pos]) T(val);
    size_++;
  }

  void insert(size_t pos, size_t count, const T &val) {
    if (count == 0)
      return;
    if (pos > size_)
      pos = size_;
    ensure_capacity(size_ + count);
    // Move existing elements to make room
    for (size_t i = size_ + count - 1; i >= pos + count; i--) {
      new (&data_[i]) T(static_cast<T &&>(data_[i - count]));
      data_[i - count].~T();
    }
    // Insert new elements
    for (size_t i = 0; i < count; i++) {
      new (&data_[pos + i]) T(val);
    }
    size_ += count;
  }

  void erase(size_t pos) {
    if (pos >= size_)
      return;
    // Destroy element at pos
    data_[pos].~T();
    // Move elements after pos forward
    for (size_t i = pos; i < size_ - 1; i++) {
      new (&data_[i]) T(static_cast<T &&>(data_[i + 1]));
      data_[i + 1].~T();
    }
    size_--;
  }

  void erase(size_t pos, size_t count) {
    if (count == 0 || pos >= size_)
      return;
    // Clamp count to not go beyond end
    if (pos + count > size_)
      count = size_ - pos;
    // Destroy elements in range
    for (size_t i = pos; i < pos + count; i++) {
      data_[i].~T();
    }
    // Move elements after erased range forward
    for (size_t i = pos; i < size_ - count; i++) {
      new (&data_[i]) T(static_cast<T &&>(data_[i + count]));
      data_[i + count].~T();
    }
    size_ -= count;
  }

  // Iterator support (minimal)
  T *begin() { return data_; }
  T *end() { return data_ + size_; }
  const T *begin() const { return data_; }
  const T *end() const { return data_ + size_; }
};

} // namespace ou

#endif
