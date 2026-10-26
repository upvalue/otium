#ifndef OTIUM_PROTOTYPE_FOREIGN_TYPES_H
#define OTIUM_PROTOTYPE_FOREIGN_TYPES_H

#include "internal.h"
#include "foreign.h"
#include <ffi.h>

/* Private ABI metadata. Type preparation never allocates in the moving heap. */
enum { T_VOID, T_BOOL, T_SIGNED, T_UNSIGNED, T_FLOAT, T_DOUBLE, T_POINTER, T_CALLBACK, T_UTF8 };

typedef struct {
  const char *name, *c_type, *member;
  unsigned kind;
  size_t size, alignment;
  int64_t minimum;
  uint64_t maximum;
} Scalar;

#define MAX_ARGUMENTS 64

enum { AGGREGATE = 23, UNION_TYPE = 24, ARRAY_TYPE = 25 };
typedef struct AbiNode AbiNode;

struct AbiNode {
  AbiNode* next;
  ffi_type type;
};
typedef struct Signature Signature;

struct Signature {
  Signature* next;
  size_t count;
  unsigned arguments[MAX_ARGUMENTS], result;
  ffi_cif cif;
  ffi_type *argument_types[MAX_ARGUMENTS], *result_type;
  AbiNode* nodes;
  bool marked, pending;
};

extern const Scalar foreign_scalars[AGGREGATE];
int foreign_scalar(Runtime* rt, Value name, bool memory);
int foreign_scalar_code(Runtime* rt, Value value, bool memory);
bool foreign_list_length(Runtime* rt, Value list, size_t limit, size_t* count);
void foreign_free_abi_nodes(AbiNode* node);
Signature* foreign_signature(Runtime* rt, Signature** cache, Value arguments, Value result,
                             bool codes);

static inline Value list_item(Value list, size_t index) {
  while (index--) list = object_fields(list)[1];
  return object_fields(list)[0];
}

#endif
