// value.hpp — the 16-byte tagged Value and inline constructors/tests.
#pragma once
#include "common.hpp"

namespace ot {

enum class Tag : u8 {
  Nil,
  Null,  // Null = the empty list ()
  False,
  True,
  Int,
  Float,
  Symbol,
  Keyword,  // payload: u32 intern id (immediates, not heap)
  String,
  Pair,
  Array,
  Table,
  Buffer,
  Function,
  Macro,
  Param,
  Restart,
  Unwind,  // internal sentinel: an unwind is in flight
};

struct Obj;  // heap object header, defined in heap.hpp

struct Value {  // 16 bytes
  Tag tag;
  union {
    i64 i;
    f64 f;
    u32 id;
    Obj* obj;
  };
};
static_assert(sizeof(Value) == 16, "Value must be 16 bytes");

// constructors
inline Value nil_v() {
  Value v;
  v.tag = Tag::Nil;
  v.i = 0;
  return v;
}
inline Value null_v() {
  Value v;
  v.tag = Tag::Null;
  v.i = 0;
  return v;
}
inline Value bool_v(bool b) {
  Value v;
  v.tag = b ? Tag::True : Tag::False;
  v.i = 0;
  return v;
}
inline Value int_v(i64 i) {
  Value v;
  v.tag = Tag::Int;
  v.i = i;
  return v;
}
inline Value float_v(f64 f) {
  Value v;
  v.tag = Tag::Float;
  v.f = f;
  return v;
}
inline Value symbol_v(u32 id) {
  Value v;
  v.tag = Tag::Symbol;
  v.i = 0;
  v.id = id;
  return v;
}
inline Value keyword_v(u32 id) {
  Value v;
  v.tag = Tag::Keyword;
  v.i = 0;
  v.id = id;
  return v;
}
inline Value obj_v(Tag t, Obj* o) {
  Value v;
  v.tag = t;
  v.obj = o;
  return v;
}
inline Value unwind_v() {
  Value v;
  v.tag = Tag::Unwind;
  v.i = 0;
  return v;
}

// tests
inline bool is_nil(Value v) { return v.tag == Tag::Nil; }
inline bool is_falsy(Value v) { return v.tag == Tag::Nil || v.tag == Tag::False; }
inline bool is_truthy(Value v) { return !is_falsy(v); }
inline bool is_unwind(Value v) { return v.tag == Tag::Unwind; }
inline bool is_heap(Value v) { return v.tag >= Tag::String && v.tag <= Tag::Restart; }

// eq? semantics: identity for heap values, value equality for immediates.
inline bool val_eq(Value a, Value b) {
  if (a.tag != b.tag) return false;
  switch (a.tag) {
    case Tag::Nil:
    case Tag::Null:
    case Tag::False:
    case Tag::True:
    case Tag::Unwind: return true;
    case Tag::Int: return a.i == b.i;
    case Tag::Float: return a.f == b.f;
    case Tag::Symbol:
    case Tag::Keyword: return a.id == b.id;
    default: return a.obj == b.obj;
  }
}

struct Vm;
// equal? semantics (deep) — defined in builtins/data.cpp
bool val_equal(Vm& vm, Value a, Value b);

#define OT_TRY(expr)                                                                               \
  {                                                                                                \
    ::ot::Value _v = (expr);                                                                       \
    if (_v.tag == ::ot::Tag::Unwind) return _v;                                                    \
  }

}  // namespace ot
