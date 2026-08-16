#pragma once
#include "common.hpp"
#include "value.hpp"

namespace ot {

struct Vm;

struct Reader {
  Reader(Vm& vm, const char* src, u32 len, const char* filename);

  // Reads one form. Returns the form, or nil_v() with atEof()==true when the
  // input is exhausted, or a Tag::Unwind value on a read error.
  Value next();
  bool atEof() const { return eof_; }

private:
  Vm& vm_;
  const char* src_;
  u32 len_;
  [[maybe_unused]] const char* filename_;  // reserved for position records
  u32 pos_ = 0;
  u32 line_ = 1;
  u32 col_ = 1;
  bool eof_ = false;

  // helpers
  bool eof() const { return pos_ >= len_; }
  char peek() const { return src_[pos_]; }
  char peekAt(u32 off) const { return pos_ + off < len_ ? src_[pos_ + off] : '\0'; }
  void advance();
  void skipWs();

  Value err(u32 line, u32 col, const char* what);
  Value readForm();
  Value readList(char close, u32 line, u32 col, const char* ctorSym);
  Value readString(u32 line, u32 col);
  Value readSugar(const char* sym, u32 symLen);
  Value readAtom();
  Value classifyAtom(const char* tok, u32 n, u32 line, u32 col);
  Value parseNumber(const char* tok, u32 n, u32 line, u32 col);
};

}  // namespace ot
