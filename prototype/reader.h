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
} AstKind;
typedef struct Ast Ast;

struct Ast {
  AstKind kind;
  size_t offset;
  uint64_t symbol_id;
  Ast* next;

  union {
    int64_t integer;
    double real;

    struct {
      const char* data;
      size_t size;
    } text;

    struct {
      Ast *first, *tail;
    } list;
  } as;
};

typedef struct ArenaBlock ArenaBlock;

typedef struct {
  const char* source;
  size_t length, position, error_offset;
  const char* error;
  bool incomplete;
  ArenaBlock* blocks;
  size_t allocated, budget;
  unsigned depth;
} Reader;

/* Source must remain alive until reader_destroy. Nodes belong to this reader.
 * The compiler arena is separate from the managed runtime heap. */
void reader_init(Reader* r, const char* source, size_t length, size_t budget);
void reader_destroy(Reader* r);
void* reader_allocate(Reader* r, size_t size);
/* NULL with no error means end of source. Errors are sticky. */
Ast* reader_next(Reader* r);
Ast* reader_one(Reader* r);
bool ast_symbol(const Ast* a, const char* name);

#endif
