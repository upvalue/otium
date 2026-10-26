#ifndef OTIUM_PROTOTYPE_READER_H
#define OTIUM_PROTOTYPE_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
  AST_NIL,
  AST_BOOL,
  AST_INT,
  AST_FLOAT,
  AST_CHAR,
  AST_STRING,
  AST_SYMBOL,
  AST_KEYWORD,
  AST_LIST,
  /* Runtime syntax only: an index into the rooted constant table. */
  AST_VALUE
} oti_ast_kind;
typedef struct oti_ast oti_ast;

struct oti_ast {
  oti_ast_kind kind;
  size_t offset;
  uint64_t symbol_id;
  oti_ast* next;

  union {
    int64_t integer;
    double real;

    struct {
      const char* data;
      size_t size;
    } text;

    struct {
      oti_ast *first, *tail;
    } list;
  } as;
};

typedef struct oti_arena_block oti_arena_block;

typedef struct {
  const char* source;
  size_t length, position, error_offset;
  const char* error;
  const char* error_code;
  bool incomplete;
  oti_arena_block* blocks;
  size_t allocated, budget;
  unsigned depth;
} oti_reader;

/* Source must remain alive until oti_reader_destroy. Nodes belong to this reader.
 * The compiler arena is separate from the managed runtime heap. */
void oti_reader_init(oti_reader* r, const char* source, size_t length, size_t budget);
void oti_reader_destroy(oti_reader* r);
void* oti_reader_allocate(oti_reader* r, size_t size);
/* NULL with no error means end of source. Errors are sticky. */
oti_ast* oti_reader_next(oti_reader* r);
oti_ast* oti_reader_one(oti_reader* r);
bool oti_ast_symbol(const oti_ast* a, const char* name);

#endif
