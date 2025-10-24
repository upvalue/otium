// result.hpp - A Result<T,E> type for error handling
#ifndef _RESULT_HPP
#define _RESULT_HPP

// Placement new operator (needed for environments without <new>)
#ifndef OT_POSIX
inline void* operator new(size_t, void* p) noexcept { return p; }
inline void operator delete(void*, void*) noexcept {}
#else
#include <new>
#endif

// Result type similar to Rust's Result<T,E>
// Stores either a success value of type T or an error value of type E
template<typename T, typename E = bool>
class Result {
private:
  bool success_;
  union {
    T value_;
    E error_;
  };

public:
  // Constructors are private, use factory methods
  Result(const Result& other) : success_(other.success_) {
    if (success_) {
      new (&value_) T(other.value_);
    } else {
      new (&error_) E(other.error_);
    }
  }

  Result(Result&& other) : success_(other.success_) {
    if (success_) {
      new (&value_) T(static_cast<T&&>(other.value_));
    } else {
      new (&error_) E(static_cast<E&&>(other.error_));
    }
  }

  ~Result() {
    if (success_) {
      value_.~T();
    } else {
      error_.~E();
    }
  }

  Result& operator=(const Result& other) {
    if (this != &other) {
      if (success_) {
        value_.~T();
      } else {
        error_.~E();
      }
      success_ = other.success_;
      if (success_) {
        new (&value_) T(other.value_);
      } else {
        new (&error_) E(other.error_);
      }
    }
    return *this;
  }

  Result& operator=(Result&& other) {
    if (this != &other) {
      if (success_) {
        value_.~T();
      } else {
        error_.~E();
      }
      success_ = other.success_;
      if (success_) {
        new (&value_) T(static_cast<T&&>(other.value_));
      } else {
        new (&error_) E(static_cast<E&&>(other.error_));
      }
    }
    return *this;
  }

  // Factory methods for creating Result instances
  static Result ok(const T& value) {
    Result r;
    r.success_ = true;
    new (&r.value_) T(value);
    return r;
  }

  static Result ok(T&& value) {
    Result r;
    r.success_ = true;
    new (&r.value_) T(static_cast<T&&>(value));
    return r;
  }

  static Result err(const E& error) {
    Result r;
    r.success_ = false;
    new (&r.error_) E(error);
    return r;
  }

  static Result err(E&& error) {
    Result r;
    r.success_ = false;
    new (&r.error_) E(static_cast<E&&>(error));
    return r;
  }

  // Query methods
  bool is_ok() const { return success_; }
  bool is_err() const { return !success_; }

  // Value access (unsafe - undefined behavior if wrong state)
  T& value() { return value_; }
  const T& value() const { return value_; }

  E& error() { return error_; }
  const E& error() const { return error_; }

  // Safe value access with default
  T value_or(const T& default_val) const {
    if (success_) {
      return value_;
    }
    return default_val;
  }

  T value_or(T&& default_val) const {
    if (success_) {
      return value_;
    }
    return static_cast<T&&>(default_val);
  }

  // Ergonomic operators
  operator bool() const { return success_; }

  T& operator*() { return value_; }
  const T& operator*() const { return value_; }

  T* operator->() { return &value_; }
  const T* operator->() const { return &value_; }

private:
  // Private default constructor for factory methods
  Result() {}
};

// Convenience typedef for simple success/failure with bool error
template<typename T>
using BoolResult = Result<T, bool>;

#endif // _RESULT_HPP