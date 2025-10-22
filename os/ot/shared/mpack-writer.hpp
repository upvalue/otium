#ifndef OT_SHARED_MPACK_WRITER_HPP
#define OT_SHARED_MPACK_WRITER_HPP

#include "ot/common.h"

#include "ot/shared/mpack.h"
#include "ot/shared/string-view.hpp"

// Simple C++ wrapper around libmpack for easier serialization
// Usage:
//   char buf[256];
//   MPackWriter msg(buf, sizeof(buf));
//   msg.stringarray(argc, argv);
//   send(msg.data(), msg.size());
//
class MPackWriter {
private:
  char *buf_start_; // Original buffer start
  char *buf_;       // Current write position
  size_t buflen_;   // Remaining space
  size_t capacity_; // Total buffer capacity
  mpack_tokbuf_t state_;
  bool error_;

  // Internal: write a token, tracking errors
  void write_token(const mpack_token_t &tok) {
    if (error_)
      return;
    int result = mpack_write(&state_, &buf_, &buflen_, &tok);
    if (result != MPACK_OK) {
      error_ = true;
    }
  }

public:
  // Initialize packer with a buffer
  MPackWriter(void *buffer, size_t size)
      : buf_start_((char *)buffer), buf_((char *)buffer), buflen_(size),
        capacity_(size), error_(false) {
    mpack_tokbuf_init(&state_);
  }

  // Reset to reuse the same buffer
  void reset() {
    buf_ = buf_start_;
    buflen_ = capacity_;
    error_ = false;
    mpack_tokbuf_init(&state_);
  }

  // ===== Basic Types =====

  MPackWriter &nil() {
    write_token(mpack_pack_nil());
    return *this;
  }

  MPackWriter &pack(bool v) {
    write_token(mpack_pack_boolean(v ? 1u : 0u));
    return *this;
  }

  MPackWriter &pack(uint32_t v) {
    write_token(mpack_pack_uint(v));
    return *this;
  }

  MPackWriter &pack(int32_t v) {
    write_token(mpack_pack_sint(v));
    return *this;
  }

  // ===== Strings =====

  // Pack null-terminated string
  MPackWriter &str(const char *s) {
    if (!s || error_)
      return *this;
    uint32_t len = 0;
    const char *p = s;
    while (*p++)
      len++;
    return str(s, len);
  }

  // Pack string with explicit length
  MPackWriter &str(const char *s, uint32_t len) {
    if (error_)
      return *this;
    write_token(mpack_pack_str(len));
    write_token(mpack_pack_chunk(s, len));
    return *this;
  }

  MPackWriter &str(const StringView &sv) { return str(sv.ptr, sv.len); }

  // ===== Binary Data =====

  MPackWriter &bin(const void *data, uint32_t len) {
    if (error_)
      return *this;
    write_token(mpack_pack_bin(len));
    write_token(mpack_pack_chunk((const char *)data, len));
    return *this;
  }

  // ===== Collections =====

  // Start an array of N elements (caller must pack N items after this)
  MPackWriter &array(uint32_t count) {
    write_token(mpack_pack_array(count));
    return *this;
  }

  // Start a map of N key-value pairs (caller must pack 2*N items)
  MPackWriter &map(uint32_t count) {
    write_token(mpack_pack_map(count));
    return *this;
  }

  // ===== Convenience Methods =====

  // Pack argc/argv as array of strings
  MPackWriter &stringarray(int argc, char **argv) {
    array(argc);
    for (int i = 0; i < argc; i++) {
      str(argv[i]);
    }
    return *this;
  }

  // ===== State Query =====

  // Check if all operations succeeded
  bool ok() const { return !error_; }

  // Get bytes written so far
  uint32_t size() const { return capacity_ - buflen_; }

  // Get pointer to packed data
  const void *data() const { return buf_start_; }

  // Get remaining buffer space
  uint32_t remaining() const { return buflen_; }
};

#endif // OT_SHARED_MPACK_WRITER_HPP
