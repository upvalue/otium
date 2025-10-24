// result.hpp - A Result<T,E> type for error handling
#ifndef _RESULT_HPP
#define _RESULT_HPP

// Result type similar to Rust's Result<T,E>
// Stores either a success value of type T or an error value of type E
// Note: Both value and error are allocated, but only one is valid based on success_
template<typename T, typename E = bool>
class Result {
private:
  bool success_;
  T value_;
  E error_;

  // Private constructors for factory methods
  Result(bool success, const T& value, const E& error)
    : success_(success), value_(value), error_(error) {}

public:
  // Default copy and move constructors/assignment work fine
  Result(const Result&) = default;
  Result(Result&&) = default;
  Result& operator=(const Result&) = default;
  Result& operator=(Result&&) = default;
  ~Result() = default;

  // Factory methods for creating Result instances
  static Result ok(const T& value) {
    return Result(true, value, E{});
  }

  static Result ok(T&& value) {
    return Result(true, static_cast<T&&>(value), E{});
  }

  static Result err(const E& error) {
    return Result(false, T{}, error);
  }

  static Result err(E&& error) {
    return Result(false, T{}, static_cast<E&&>(error));
  }

  // Query methods
  bool is_ok() const { return success_; }
  bool is_err() const { return !success_; }

  // Value access (only valid when is_ok())
  T& value() { return value_; }
  const T& value() const { return value_; }

  // Error access (only valid when is_err())
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
};

// Convenience typedef for simple success/failure with bool error
template<typename T>
using BoolResult = Result<T, bool>;

#endif // _RESULT_HPP