// vec.h — growable vector (type-erased core + typed views) and byte/string builder.
//
// Every typed vector is a struct { T* data; u32 len; u32 cap; }. The growth
// logic lives once in vecraw_reserve, which the typed macros reach by casting
// to VecRaw — layout-compatible by construction (pointer + two u32). Elements
// must be trivially copyable (they always were: Vec<T> required it).
#pragma once
#include "common.h"
#include <stdarg.h>

typedef struct VecRaw {
  void* data;
  u32 len;
  u32 cap;
} VecRaw;

void vecraw_reserve(VecRaw* v, u32 n, size_t elemSize);  // ot_fatal on OOM
void vecraw_deinit(VecRaw* v);                           // frees storage, zeroes fields

// Declares a distinct typed vector struct. Instances zero-init: `VecValue v = {0};`
#define OT_VEC_TYPE(T, Name)                                                                       \
  typedef struct Name {                                                                            \
    T* data;                                                                                       \
    u32 len;                                                                                       \
    u32 cap;                                                                                       \
  } Name

// Generic operations over any typed vector (pass a pointer to the vector).
#define vec_reserve(v, n) vecraw_reserve((VecRaw*)(v), (n), sizeof(*(v)->data))
#define vec_push(v, x)                                                                             \
  do {                                                                                             \
    vec_reserve((v), (v)->len + 1);                                                                \
    (v)->data[(v)->len++] = (x);                                                                   \
  } while (0)
static inline u32 ot_checked_index(u32 i, u32 len) {
  OT_ASSERT(i < len);
  return i;
}
#define vec_pop(v) ((v)->len = ot_checked_index((v)->len - 1, (v)->len), (v)->data[(v)->len])
#define vec_at(v, i) ((v)->data[ot_checked_index((i), (v)->len)])
#define vec_clear(v) ((void)((v)->len = 0))
#define vec_deinit(v) vecraw_deinit((VecRaw*)(v))

OT_VEC_TYPE(u32, VecU32);
OT_VEC_TYPE(char, VecChar);

typedef struct Buf {
  char* data;
  u32 len;
  u32 cap;
} Buf;

void buf_append(Buf* b, const char* s, u32 n);
void buf_append_cstr(Buf* b, const char* s);
void buf_printf(Buf* b, const char* fmt, ...);
static inline void buf_deinit(Buf* b) { vecraw_deinit((VecRaw*)b); }
static inline void buf_clear(Buf* b) { b->len = 0; }
