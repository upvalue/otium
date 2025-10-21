#include "ot/shared/mpack-reader.hpp"

MPackReader::MPackReader(const void* buffer, size_t size)
  : buf_((const char*)buffer)
  , buflen_(size)
  , error_(false)
{
  mpack_tokbuf_init(&state_);
}

bool MPackReader::read_next(mpack_token_t& tok) {
  if (error_) return false;

  int result = mpack_read(&state_, &buf_, &buflen_, &tok);
  if (result != MPACK_OK) {
    error_ = true;
    return false;
  }
  return true;
}

mpack_token_type_t MPackReader::peek_type() {
  if (error_ || buflen_ == 0) {
    return (mpack_token_type_t)0;  // Invalid type
  }

  // Peek at first byte to determine type
  // This is a simplified peek - for full implementation would need
  // to save/restore state
  unsigned char first_byte = (unsigned char)buf_[0];

  if (first_byte < 0x80) return MPACK_TOKEN_UINT;
  if (first_byte < 0x90) return MPACK_TOKEN_MAP;
  if (first_byte < 0xa0) return MPACK_TOKEN_ARRAY;
  if (first_byte < 0xc0) return MPACK_TOKEN_STR;

  switch (first_byte) {
    case 0xc0: return MPACK_TOKEN_NIL;
    case 0xc2:
    case 0xc3: return MPACK_TOKEN_BOOLEAN;
    case 0xc4:
    case 0xc5:
    case 0xc6: return MPACK_TOKEN_BIN;
    case 0xcc:
    case 0xcd:
    case 0xce:
    case 0xcf: return MPACK_TOKEN_UINT;
    case 0xd0:
    case 0xd1:
    case 0xd2:
    case 0xd3: return MPACK_TOKEN_SINT;
    case 0xd9:
    case 0xda:
    case 0xdb: return MPACK_TOKEN_STR;
    case 0xdc:
    case 0xdd: return MPACK_TOKEN_ARRAY;
    case 0xde:
    case 0xdf: return MPACK_TOKEN_MAP;
    default: return (mpack_token_type_t)0;
  }
}

bool MPackReader::read_nil() {
  mpack_token_t tok;
  if (!read_next(tok) || tok.type != MPACK_TOKEN_NIL) {
    error_ = true;
    return false;
  }
  return true;
}

bool MPackReader::read_bool(bool& value) {
  mpack_token_t tok;
  if (!read_next(tok) || tok.type != MPACK_TOKEN_BOOLEAN) {
    error_ = true;
    return false;
  }
  value = mpack_unpack_boolean(tok);
  return true;
}

bool MPackReader::read_uint(uint32_t& value) {
  mpack_token_t tok;
  if (!read_next(tok) || tok.type != MPACK_TOKEN_UINT) {
    error_ = true;
    return false;
  }

  // Only support 32-bit values
  if (tok.data.value.hi != 0) {
    error_ = true;
    return false;
  }

  value = tok.data.value.lo;
  return true;
}

bool MPackReader::read_int(int32_t& value) {
  mpack_token_t tok;
  if (!read_next(tok)) {
    error_ = true;
    return false;
  }

  // Accept both SINT and UINT (positive values may be packed as UINT)
  if (tok.type == MPACK_TOKEN_UINT) {
    // Positive value packed as uint
    if (tok.data.value.hi != 0 || tok.data.value.lo > 0x7FFFFFFF) {
      error_ = true;
      return false;
    }
    value = (int32_t)tok.data.value.lo;
    return true;
  } else if (tok.type == MPACK_TOKEN_SINT) {
    mpack_sintmax_t val = mpack_unpack_sint(tok);

    // Check if in 32-bit range
    if (val < -2147483648LL || val > 2147483647LL) {
      error_ = true;
      return false;
    }

    value = (int32_t)val;
    return true;
  } else {
    error_ = true;
    return false;
  }
}

bool MPackReader::read_string(StringView& str) {
  mpack_token_t tok;

  // Read string header
  if (!read_next(tok) || tok.type != MPACK_TOKEN_STR) {
    error_ = true;
    return false;
  }

  str.len = tok.length;

  // Read chunk (the actual string data)
  if (!read_next(tok) || tok.type != MPACK_TOKEN_CHUNK) {
    error_ = true;
    return false;
  }

  str.ptr = tok.data.chunk_ptr;  // Zero-copy!
  return true;
}

bool MPackReader::enter_array(uint32_t& count) {
  mpack_token_t tok;
  if (!read_next(tok) || tok.type != MPACK_TOKEN_ARRAY) {
    error_ = true;
    return false;
  }
  count = tok.length;
  return true;
}

bool MPackReader::enter_map(uint32_t& count) {
  mpack_token_t tok;
  if (!read_next(tok) || tok.type != MPACK_TOKEN_MAP) {
    error_ = true;
    return false;
  }
  count = tok.length;
  return true;
}

bool MPackReader::read_stringarray(StringView* views, size_t max_count, size_t& actual_count) {
  uint32_t count;
  if (!enter_array(count)) {
    return false;
  }

  if (count > max_count) {
    error_ = true;
    return false;
  }

  actual_count = count;

  for (uint32_t i = 0; i < count; i++) {
    if (!read_string(views[i])) {
      return false;
    }
  }

  return true;
}

bool MPackReader::read_args_map(StringView* argv_views, size_t max_args, size_t& argc) {
  uint32_t pairs;
  if (!enter_map(pairs)) {
    return false;
  }

  if (pairs != 1) {
    error_ = true;
    return false;
  }

  // Read the key - should be "args"
  StringView key;
  if (!read_string(key)) {
    return false;
  }

  if (!key.equals("args")) {
    error_ = true;
    return false;
  }

  // Read the value - should be an array of strings
  return read_stringarray(argv_views, max_args, argc);
}
