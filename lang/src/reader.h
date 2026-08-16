#pragma once
#include "common.h"
#include "value.h"

typedef struct State State;

// Fields below `// internal` are private to reader.c.
typedef struct Reader {
  State* vm;
  const char* src;
  u32 len;
  const char* filename;  // reserved for position records
  u32 pos;
  u32 line;
  u32 col;
  bool eofFlag;
  bool incompleteFlag;
} Reader;

void reader_init(Reader* r, State* vm, const char* src, u32 len, const char* filename);

// Reads one form. Returns the form, or nil_v() with reader_at_eof()==true when
// the input is exhausted, or a Tag_Unwind value on a read error.
Value reader_next(Reader* r);
static inline bool reader_at_eof(const Reader* r) { return r->eofFlag; }
static inline bool reader_incomplete(const Reader* r) { return r->incompleteFlag; }
static inline u32 reader_position(const Reader* r) { return r->pos; }
