#ifndef OT_SHARED_MPACK_READER_HPP
#define OT_SHARED_MPACK_READER_HPP

#include "ot/common.h"
#include "ot/lib/error-codes.hpp"
#include "ot/lib/mpack/mpack.h"
#include "ot/lib/string-view.hpp"

// Zero-copy reader for msgpack data
class MPackReader {
private:
  const char* buf_;         // Current read position
  size_t buflen_;           // Remaining bytes
  mpack_tokbuf_t state_;    // mpack reading state
  bool error_;              // Error state

  // Internal: Read next token
  bool read_next(mpack_token_t& tok);

public:
  // Initialize reader with a buffer
  MPackReader(const void* buffer, size_t size);

  // ===== Type Checking =====

  // Peek at next token type without consuming
  mpack_token_type_t peek_type();

  // ===== Basic Type Reading =====

  // Read nil (returns false if not nil)
  bool read_nil();

  // Read boolean value
  bool read_bool(bool& value);

  // Read unsigned integer
  bool read_uint(uint32_t& value);

  // Read signed integer
  bool read_int(int32_t& value);

  // Read error code
  bool read_error_code(ErrorCode& value);

  // Read string (zero-copy - returns view into msgpack buffer)
  bool read_string(StringView& str);

  // Read binary data (zero-copy - returns view into msgpack buffer)
  bool read_bin(StringView& bin);

  // Convenience method aliases for generated code
  bool read_u32(uint32_t& value) { return read_uint(value); }
  bool read_u64(uint64_t& value) {
    uint32_t v32;
    if (read_uint(v32)) {
      value = v32;
      return true;
    }
    return false;
  }
  bool read_i32(int32_t& value) { return read_int(value); }
  bool read_i64(int64_t& value) {
    int32_t v32;
    if (read_int(v32)) {
      value = v32;
      return true;
    }
    return false;
  }

  // ===== Container Reading =====

  // Enter array, returns element count
  bool enter_array(uint32_t& count);

  // Enter map, returns pair count
  bool enter_map(uint32_t& count);

  // Convenience methods for generated code
  bool is_array() { return peek_type() == MPACK_TOKEN_ARRAY; }
  bool is_map() { return peek_type() == MPACK_TOKEN_MAP; }

  uint32_t array_size() {
    uint32_t count = 0;
    if (enter_array(count)) return count;
    return 0;
  }

  uint32_t map_size() {
    uint32_t count = 0;
    if (enter_map(count)) return count;
    return 0;
  }

  // ===== Convenience Methods =====

  // Read array of strings (zero-copy)
  // fills views array with StringViews pointing into msgpack buffer
  bool read_stringarray(StringView* views, size_t max_count, size_t& actual_count);

  // Read the kernel args structure: {"args": [...]}
  // Returns StringViews pointing directly into msgpack buffer - NO ALLOCATION
  bool read_args_map(StringView* argv_views, size_t max_args, size_t& argc);

  // ===== State Query =====

  bool ok() const { return !error_; }
  size_t bytes_remaining() const { return buflen_; }
};

#endif  // OT_SHARED_MPACK_READER_HPP
