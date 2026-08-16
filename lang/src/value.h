// value.h — the 16-byte tagged Value and inline constructors/tests.
#pragma once
#include "common.h"

typedef enum Tag : u8 {
  Tag_Nil,
  Tag_Null,  // Null = the empty list ()
  Tag_False,
  Tag_True,
  Tag_Int,
  Tag_Float,
  Tag_Symbol,
  Tag_Keyword,  // payload: u32 intern id (immediates, not heap)
  Tag_String,
  Tag_Pair,
  Tag_Array,
  Tag_Table,
  Tag_Buffer,
  Tag_Code,
  Tag_Function,
  Tag_Macro,
  Tag_Param,
  Tag_Restart,
  Tag_Foreign,
  Tag_Unwind,  // internal sentinel: an unwind is in flight
} Tag;

typedef struct Obj Obj;  // heap object header, defined in heap.h

typedef struct Value {  // 16 bytes
  Tag tag;
  union {
    i64 i;
    f64 f;
    u32 id;
    Obj* obj;
  };
} Value;
static_assert(sizeof(Value) == 16, "Value must be 16 bytes");

// constructors
static inline Value nil_v(void) { return (Value){.tag = Tag_Nil, .i = 0}; }
static inline Value null_v(void) { return (Value){.tag = Tag_Null, .i = 0}; }
static inline Value bool_v(bool b) { return (Value){.tag = b ? Tag_True : Tag_False, .i = 0}; }
static inline Value int_v(i64 i) { return (Value){.tag = Tag_Int, .i = i}; }
static inline Value float_v(f64 f) { return (Value){.tag = Tag_Float, .f = f}; }
static inline Value symbol_v(u32 id) { return (Value){.tag = Tag_Symbol, .id = id}; }
static inline Value keyword_v(u32 id) { return (Value){.tag = Tag_Keyword, .id = id}; }
static inline Value obj_v(Tag t, Obj* o) { return (Value){.tag = t, .obj = o}; }
static inline Value unwind_v(void) { return (Value){.tag = Tag_Unwind, .i = 0}; }

// tests
static inline bool is_nil(Value v) { return v.tag == Tag_Nil; }
static inline bool is_falsy(Value v) { return v.tag == Tag_Nil || v.tag == Tag_False; }
static inline bool is_truthy(Value v) { return !is_falsy(v); }
static inline bool is_unwind(Value v) { return v.tag == Tag_Unwind; }
static inline bool is_heap(Value v) { return v.tag >= Tag_String && v.tag <= Tag_Foreign; }

// eq? semantics: identity for heap values, value equality for immediates.
static inline bool val_eq(Value a, Value b) {
  if (a.tag != b.tag) return false;
  switch (a.tag) {
    case Tag_Nil:
    case Tag_Null:
    case Tag_False:
    case Tag_True:
    case Tag_Unwind: return true;
    case Tag_Int: return a.i == b.i;
    case Tag_Float: return a.f == b.f;
    case Tag_Symbol:
    case Tag_Keyword: return a.id == b.id;
    default: return a.obj == b.obj;
  }
}

#define OT_TRY(expr)                                                                               \
  {                                                                                                \
    Value _v = (expr);                                                                             \
    if (_v.tag == Tag_Unwind) return _v;                                                           \
  }

// Scoped variant: pops the value-stack scope opened at `sc` before propagating.
// Use inside any region opened with scope_begin (see state.h).
#define OT_TRYS(vm, sc, expr)                                                                      \
  {                                                                                                \
    Value _v = (expr);                                                                             \
    if (_v.tag == Tag_Unwind) {                                                                    \
      scope_pop_to((vm), (sc));                                                                    \
      return _v;                                                                                   \
    }                                                                                              \
  }
