#pragma once
#include "common.h"
#include "slots.h"
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

// Returns nil on success or the unwind
// sentinel on a read error, writing the form (or nil at EOF) into dst.
Value reader_next_ref(Reader* r, Ref dst);
static inline bool reader_at_eof(const Reader* r) { return r->eofFlag; }
static inline bool reader_incomplete(const Reader* r) { return r->incompleteFlag; }
static inline u32 reader_position(const Reader* r) { return r->pos; }
