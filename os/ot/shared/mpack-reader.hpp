#ifndef OT_SHARED_MPACK_READER_HPP
#define OT_SHARED_MPACK_READER_HPP

#include "ot/common.h"
#include "ot/shared/mpack.h"
#include "ot/shared/string-view.hpp"

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

  // Read string (zero-copy - returns view into msgpack buffer)
  bool read_string(StringView& str);

  // ===== Container Reading =====

  // Enter array, returns element count
  bool enter_array(uint32_t& count);

  // Enter map, returns pair count
  bool enter_map(uint32_t& count);

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
