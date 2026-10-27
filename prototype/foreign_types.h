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
} oti_scalar;

#define MAX_ARGUMENTS 64

enum { AGGREGATE = 23, UNION_TYPE = 24, ARRAY_TYPE = 25 };
typedef struct oti_abi_node oti_abi_node;

struct oti_abi_node {
  oti_abi_node* next;
  ffi_type type;
};
typedef struct oti_signature oti_signature;

struct oti_signature {
  oti_signature* next;
  size_t count;
  unsigned arguments[MAX_ARGUMENTS], result;
  ffi_cif cif;
  ffi_type *argument_types[MAX_ARGUMENTS], *result_type;
  oti_abi_node* nodes;
  bool marked, pending;
};

extern const oti_scalar oti_foreign_scalars[AGGREGATE];
int oti_foreign_scalar(ot_state* rt, ot_value name, bool memory);
int oti_foreign_scalar_code(ot_state* rt, ot_value value, bool memory);
bool oti_foreign_list_length(ot_state* rt, ot_value list, size_t limit, size_t* count);
void oti_foreign_free_abi_nodes(oti_abi_node* node);
oti_signature* oti_foreign_signature(ot_state* rt, oti_signature** cache, ot_value arguments,
                                     ot_value result, bool codes);

static inline ot_value list_item(ot_value list, size_t index) {
  while (index--) list = object_fields(list)[1];
  return object_fields(list)[0];
}

#endif
