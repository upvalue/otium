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
  Code,
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
inline Value nil_v() { return {.tag = Tag::Nil, .i = 0}; }
inline Value null_v() { return {.tag = Tag::Null, .i = 0}; }
inline Value bool_v(bool b) { return {.tag = b ? Tag::True : Tag::False, .i = 0}; }
inline Value int_v(i64 i) { return {.tag = Tag::Int, .i = i}; }
inline Value float_v(f64 f) { return {.tag = Tag::Float, .f = f}; }
inline Value symbol_v(u32 id) { return {.tag = Tag::Symbol, .id = id}; }
inline Value keyword_v(u32 id) { return {.tag = Tag::Keyword, .id = id}; }
inline Value obj_v(Tag t, Obj* o) { return {.tag = t, .obj = o}; }
inline Value unwind_v() { return {.tag = Tag::Unwind, .i = 0}; }

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

struct State;
// equal? semantics (deep) — defined in builtins/data.cpp
bool val_equal(State& vm, Value a, Value b);

#define OT_TRY(expr)                                                                               \
  {                                                                                                \
    ::ot::Value _v = (expr);                                                                       \
    if (_v.tag == ::ot::Tag::Unwind) return _v;                                                    \
  }

}  // namespace ot
