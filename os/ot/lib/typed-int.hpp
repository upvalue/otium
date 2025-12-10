// typed-int.hpp - Type-safe integer wrapper for compile-time distinction
#ifndef OT_LIB_TYPED_INT_HPP
#define OT_LIB_TYPED_INT_HPP

#include "ot/common.h"

/**
 * A type-safe integer wrapper that prevents accidental mixing of conceptually
 * different integer types (e.g., process IDs vs process indices).
 *
 * Similar to Address<Tag>, this uses the "phantom type" pattern where Tag
 * is a zero-size type used only for compile-time type distinction.
 *
 * Example:
 *   struct PidTag {};
 *   struct PidxTag {};
 *   typedef TypedInt<uintptr_t, PidTag> Pid;
 *   typedef TypedInt<int, PidxTag> Pidx;
 *
 * This prevents accidentally passing a Pid where a Pidx is expected.
 */
template <typename T, typename Tag> class TypedInt {
private:
  T value_;

public:
  // Default constructor - zero value
  constexpr TypedInt() : value_(0) {}

  // Explicit constructor from raw value (prevents implicit conversions)
  constexpr explicit TypedInt(T val) : value_(val) {}

  // Get raw value
  T raw() const { return value_; }

  // Explicit conversion to raw type (requires static_cast)
  explicit operator T() const { return value_; }

  // Comparison operators (same type only)
  bool operator==(const TypedInt &other) const { return value_ == other.value_; }
  bool operator!=(const TypedInt &other) const { return value_ != other.value_; }
  bool operator<(const TypedInt &other) const { return value_ < other.value_; }
  bool operator<=(const TypedInt &other) const { return value_ <= other.value_; }
  bool operator>(const TypedInt &other) const { return value_ > other.value_; }
  bool operator>=(const TypedInt &other) const { return value_ >= other.value_; }

  // Comparison with raw sentinel values (for special cases like -1, 0)
  bool operator==(T raw_val) const { return value_ == raw_val; }
  bool operator!=(T raw_val) const { return value_ != raw_val; }

  // Increment/decrement (useful for counters)
  TypedInt &operator++() {
    ++value_;
    return *this;
  }

  TypedInt operator++(int) {
    TypedInt tmp = *this;
    ++value_;
    return tmp;
  }

  TypedInt &operator--() {
    --value_;
    return *this;
  }

  TypedInt operator--(int) {
    TypedInt tmp = *this;
    --value_;
    return tmp;
  }

  // Arithmetic (returns same type)
  TypedInt operator+(T offset) const { return TypedInt(value_ + offset); }
  TypedInt operator-(T offset) const { return TypedInt(value_ - offset); }

  TypedInt &operator+=(T offset) {
    value_ += offset;
    return *this;
  }

  TypedInt &operator-=(T offset) {
    value_ -= offset;
    return *this;
  }

  // Check for special sentinel values
  bool is_null() const { return value_ == 0; }
  bool is_valid() const { return value_ != 0; }

  // Explicit bool conversion
  explicit operator bool() const { return value_ != 0; }
};

/** Process ID; globally unique identifier for processes (for IPC etc) */
struct PidTag {};
/** Process index; kernel-internal index into the process table */
struct PidxTag {};
/** File handle ID; unique identifier for open file handles in filesystem */
struct FileHandleIdTag {};

// Type definitions
typedef TypedInt<uintptr_t, PidTag> Pid;
typedef TypedInt<int, PidxTag> Pidx;
typedef TypedInt<uintptr_t, FileHandleIdTag> FileHandleId;

// Special sentinel values
// Using inline constexpr to ensure single definition across translation units
inline constexpr Pidx PIDX_INVALID = Pidx(-1);
inline constexpr Pidx PIDX_NONE = Pidx(0);
inline constexpr Pid PID_NONE = Pid(0);
inline constexpr FileHandleId FILE_HANDLE_INVALID = FileHandleId(0);

#endif
