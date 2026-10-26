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

/* Syntax trees are allocated from an arena and freed all at once. Expanded and
 * rewritten forms share nodes with the forms they came from, so freeing nodes
 * one at a time would need ownership tracking. Arenas live outside the managed
 * heap and never hold managed values. */
typedef struct oti_arena_block oti_arena_block;

typedef struct {
  oti_arena_block* blocks;
  size_t allocated, limit;
} oti_arena;

void oti_arena_init(oti_arena* arena, size_t limit);
void oti_arena_destroy(oti_arena* arena);
/* Zeroed memory, or NULL when out of memory or past the arena's byte limit. */
void* oti_arena_alloc(oti_arena* arena, size_t size);

typedef struct {
  const char* source;
  size_t length, position, error_offset;
  const char* error;
  const char* error_code;
  bool incomplete;
  oti_arena arena;
  unsigned depth;
} oti_reader;

/* Source must remain alive until oti_reader_destroy. Nodes belong to the
 * reader's arena, which holds at most limit bytes. */
void oti_reader_init(oti_reader* r, const char* source, size_t length, size_t limit);
void oti_reader_destroy(oti_reader* r);
/* NULL with no error means end of source. Errors are sticky. */
oti_ast* oti_reader_next(oti_reader* r);
oti_ast* oti_reader_one(oti_reader* r);
bool oti_ast_symbol(const oti_ast* a, const char* name);
bool oti_reader_space(unsigned char c);
/* Whitespace or a character that ends a token. */
bool oti_reader_delimiter(unsigned char c);

#endif
