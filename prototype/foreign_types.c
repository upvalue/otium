#include "internal.h"
#include "foreign_types.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define SIGNED(n, t, lo, hi) {n, #t, "signed_integer", T_SIGNED, sizeof(t), _Alignof(t), lo, hi}
#define UNSIGNED(n, t, hi) {n, #t, "unsigned_integer", T_UNSIGNED, sizeof(t), _Alignof(t), 0, hi}
const oti_scalar oti_foreign_scalars[AGGREGATE] = {
    {"void", "void", "", T_VOID, 0, 1, 0, 0},
    {"bool", "bool", "boolean", T_BOOL, sizeof(bool), _Alignof(bool), 0, 0},
    SIGNED("i8", int8_t, INT8_MIN, INT8_MAX),
    UNSIGNED("u8", uint8_t, UINT8_MAX),
    SIGNED("i16", int16_t, INT16_MIN, INT16_MAX),
    UNSIGNED("u16", uint16_t, UINT16_MAX),
    SIGNED("i32", int32_t, INT32_MIN, INT32_MAX),
    UNSIGNED("u32", uint32_t, UINT32_MAX),
    SIGNED("i64", int64_t, INT64_MIN, INT64_MAX),
    UNSIGNED("u64", uint64_t, UINT64_MAX),
    SIGNED("short", short, SHRT_MIN, SHRT_MAX),
    UNSIGNED("ushort", unsigned short, USHRT_MAX),
    SIGNED("int", int, INT_MIN, INT_MAX),
    UNSIGNED("uint", unsigned int, UINT_MAX),
    SIGNED("long", long, LONG_MIN, LONG_MAX),
    UNSIGNED("ulong", unsigned long, ULONG_MAX),
    UNSIGNED("size-t", size_t, SIZE_MAX),
    SIGNED("ptrdiff-t", ptrdiff_t, PTRDIFF_MIN, PTRDIFF_MAX),
    {"float", "float", "single", T_FLOAT, sizeof(float), _Alignof(float), 0, 0},
    {"double", "double", "real", T_DOUBLE, sizeof(double), _Alignof(double), 0, 0},
    {"pointer", "void *", "pointer", T_POINTER, sizeof(void*), _Alignof(void*), 0, 0},
    {"callback-pointer", "ForeignCallback", "callback", T_CALLBACK, sizeof(oti_foreign_callback),
     _Alignof(oti_foreign_callback), 0, 0},
    {"utf8", "const char *", "pointer", T_UTF8, sizeof(char*), _Alignof(char*), 0, 0}};
#undef SIGNED
#undef UNSIGNED

int oti_foreign_scalar(ot_state* rt, ot_value name, bool memory) {
  if (oti_kind(name, K_SYMBOL)) {
    size_t length;
    memcpy(&length, object_bytes(name), sizeof(length));
    const char* s = (const char*)object_bytes(name) + sizeof(length);
    for (size_t i = 0; i < sizeof(oti_foreign_scalars) / sizeof(oti_foreign_scalars[0]); i++)
      if (strlen(oti_foreign_scalars[i].name) == length &&
          !memcmp(oti_foreign_scalars[i].name, s, length)) {
        if (memory &&
            (oti_foreign_scalars[i].kind == T_VOID || oti_foreign_scalars[i].kind == T_UTF8))
          break;
        return (int)i;
      }
  }
  oti_error(rt, "FFI type: unsupported scalar type");
  return -1;
}

bool oti_foreign_list_length(ot_state* rt, ot_value list, size_t limit, size_t* count) {
  *count = 0;
  while (oti_kind(list, K_PAIR) && *count < limit) {
    (*count)++;
    list = object_fields(list)[1];
  }
  return list == OT_EMPTY || oti_error(rt, "FFI signature: improper, cyclic or oversized list");
}

void oti_foreign_free_abi_nodes(oti_abi_node* node) {
  while (node) {
    oti_abi_node* next = node->next;
    free(node->type.elements);
    free(node);
    node = next;
  }
}

static ffi_type* abi_type(unsigned id) {
  const oti_scalar* s = &oti_foreign_scalars[id];
  switch (s->kind) {
    case T_VOID: return &ffi_type_void;
    case T_BOOL: return &ffi_type_uint8;
    case T_FLOAT: return &ffi_type_float;
    case T_DOUBLE: return &ffi_type_double;
    case T_POINTER:
    case T_CALLBACK:
    case T_UTF8: return &ffi_type_pointer;
    case T_SIGNED:
      switch (s->size) {
        case 1: return &ffi_type_sint8;
        case 2: return &ffi_type_sint16;
        case 4: return &ffi_type_sint32;
        case 8: return &ffi_type_sint64;
      }
      break;
    case T_UNSIGNED:
      switch (s->size) {
        case 1: return &ffi_type_uint8;
        case 2: return &ffi_type_uint16;
        case 4: return &ffi_type_uint32;
        case 8: return &ffi_type_uint64;
      }
  }
  return NULL;
}

int oti_foreign_scalar_code(ot_state* rt, ot_value value, bool memory) {
  if (ot_is_int(value) && ot_to_int(value) >= 0 &&
      (size_t)ot_to_int(value) < sizeof(oti_foreign_scalars) / sizeof(*oti_foreign_scalars)) {
    int code = (int)ot_to_int(value);
    if (!memory ||
        (oti_foreign_scalars[code].kind != T_VOID && oti_foreign_scalars[code].kind != T_UTF8))
      return code;
  }
  oti_error(rt, "FFI type: invalid scalar code");
  return -1;
}

static bool natural_size(ot_value v, size_t* out) {
  if (!ot_is_int(v) || ot_to_int(v) < 0) return false;
  *out = (size_t)ot_to_int(v);
  return true;
}

/* SysV AMD64 merges overlapping INTEGER/SSE leaves. Keep byte positions until
 * libffi classifies the containing aggregate: a four-aligned union can start
 * halfway through an eightbyte. No vector or long-double types are exposed.
 * See x86-64 psABI 3.2.3 and libffi src/x86/ffi64.c. */
#if defined(__x86_64__) && defined(__linux__) && !defined(__ILP32__)
static void union_classes(const ffi_type* type, size_t offset, unsigned char* classes) {
  if (type->type == FFI_TYPE_STRUCT) {
    for (size_t i = 0; type->elements[i]; i++) {
      const ffi_type* field = type->elements[i];
      offset = (offset + field->alignment - 1) / field->alignment * field->alignment;
      union_classes(field, offset, classes);
      offset += field->size;
    }
  } else {
    unsigned char kind = type->type == FFI_TYPE_FLOAT || type->type == FFI_TYPE_DOUBLE ? 1 : 2;
    for (size_t i = 0; i < type->size; i++)
      if (classes[offset + i] < kind) classes[offset + i] = kind;
  }
}

static bool lower_union(ot_state* rt, oti_signature* s, oti_abi_node* node, size_t n, size_t size,
                        size_t alignment) {
  size_t largest = 0, aligned = 1;
  for (size_t i = 0; i < n; i++) {
    ffi_type* member = node->type.elements[i];
    if (member->size > largest) largest = member->size;
    if (member->alignment > aligned) aligned = member->alignment;
  }
  if (aligned != alignment || largest > SIZE_MAX - alignment + 1 ||
      (largest + alignment - 1) / alignment * alignment != size)
    return oti_error(rt, "FFI abi: union layout mismatch");
  unsigned char* classes = calloc(size, 1);
  ffi_type** elements = calloc(size + 1, sizeof(*elements));
  oti_abi_node* padding = calloc(1, sizeof(*padding));
  if (!classes || !elements || !padding) {
    free(classes);
    free(elements);
    free(padding);
    return oti_error(rt, "FFI allocation: out of host memory");
  }
  padding->type = (ffi_type){1, 1, FFI_TYPE_STRUCT, calloc(1, sizeof(ffi_type*))};
  if (!padding->type.elements) {
    free(classes);
    free(elements);
    free(padding);
    return oti_error(rt, "FFI allocation: out of host memory");
  }
  padding->next = s->nodes;
  s->nodes = padding;
  for (size_t i = 0; i < n; i++) union_classes(node->type.elements[i], 0, classes);
  size_t count = 0;
  for (size_t i = 0; i < size;) {
    bool floating = !(i % 4) && size - i >= 4;
    for (size_t j = 0; floating && j < 4; j++) floating = classes[i + j] == 1;
    if (floating) {
      elements[count++] = &ffi_type_float;
      i += 4;
    } else {
      elements[count++] = classes[i] ? &ffi_type_uint8 : &padding->type;
      i++;
    }
  }
  free(classes);
  free(node->type.elements);
  node->type = (ffi_type){size, (unsigned short)alignment, FFI_TYPE_STRUCT, elements};
  return true;
}
#endif

/* This builds host-owned ABI trees without allocating in the moving heap. */
static ffi_type* aggregate_type(ot_state* rt, oti_signature* s, ot_value description,
                                unsigned depth, size_t* budget) {
  size_t n, kind, size, alignment;
  if (depth > 64 || !*budget) {
    oti_error(rt, "FFI signature: aggregate too complex");
    return NULL;
  }
  --*budget;
  if (!oti_foreign_list_length(rt, description, 4, &n) || n != 4 ||
      !natural_size(list_item(description, 0), &kind) ||
      !natural_size(list_item(description, 1), &size) ||
      !natural_size(list_item(description, 2), &alignment)) {
    oti_error(rt, "FFI signature: invalid type description");
    return NULL;
  }
  if (kind < AGGREGATE) {
    if (!kind || kind == 22 || size != oti_foreign_scalars[kind].size ||
        alignment != oti_foreign_scalars[kind].alignment) {
      oti_error(rt, "FFI signature: invalid aggregate member");
      return NULL;
    }
    return abi_type((unsigned)kind);
  }
  if ((kind != AGGREGATE && kind != ARRAY_TYPE && kind != UNION_TYPE) || !size || !alignment ||
      alignment > _Alignof(max_align_t)) {
    oti_error(rt, "FFI signature: invalid aggregate layout");
    return NULL;
  }
  if (size > 65536) {
    oti_error(rt, "FFI signature: aggregate too complex");
    return NULL;
  }
  ot_value members = list_item(description, 3);
  ffi_type* element = NULL;
  if (kind == ARRAY_TYPE) {
    size_t entries;
    if (!oti_foreign_list_length(rt, members, 2, &entries) || entries != 2 ||
        !natural_size(list_item(members, 1), &n)) {
      oti_error(rt, "FFI signature: invalid array layout");
      return NULL;
    }
    element = aggregate_type(rt, s, list_item(members, 0), depth + 1, budget);
    if (!element) return NULL;
  } else if (!oti_foreign_list_length(rt, members, *budget, &n)) return NULL;
  if (!n || n > *budget) {
    oti_error(rt, "FFI signature: empty or oversized aggregate");
    return NULL;
  }
  *budget -= n;
  oti_abi_node* node = calloc(1, sizeof(*node));
  if (!node) {
    oti_error(rt, "FFI allocation: out of host memory");
    return NULL;
  }
  node->next = s->nodes;
  s->nodes = node;
  node->type.type = FFI_TYPE_STRUCT;
  node->type.elements = calloc(n + 1, sizeof(ffi_type*));
  size_t* offsets = calloc(n, sizeof(size_t));
  if (!node->type.elements || !offsets) {
    free(offsets);
    oti_error(rt, "FFI allocation: out of host memory");
    return NULL;
  }
  ot_value cursor = members;
  bool ok = true;
  for (size_t i = 0; ok && i < n; i++) {
    if (element) node->type.elements[i] = element;
    else {
      ot_value field = object_fields(cursor)[0];
      size_t count;
      if (!oti_foreign_list_length(rt, field, 3, &count) || count != 3 ||
          !oti_kind(list_item(field, 0), K_SYMBOL)) {
        oti_error(rt, "FFI signature: invalid aggregate field");
        ok = false;
        break;
      }
      node->type.elements[i] = aggregate_type(rt, s, list_item(field, 1), depth + 1, budget);
      ok = node->type.elements[i] != NULL;
      cursor = object_fields(cursor)[1];
    }
  }
  if (ok && kind == UNION_TYPE) {
    cursor = members;
    for (size_t i = 0; ok && i < n; i++, cursor = object_fields(cursor)[1]) {
      size_t offset;
      if (!natural_size(list_item(object_fields(cursor)[0], 2), &offset) || offset)
        ok = oti_error(rt, "FFI abi: union field offset mismatch");
    }
#if defined(__x86_64__) && defined(__linux__) && !defined(__ILP32__)
    if (ok) ok = lower_union(rt, s, node, n, size, alignment);
#else
    if (ok) ok = oti_error(rt, "FFI abi: union calls require x86-64 Linux SysV");
#endif
    free(offsets);
    return ok ? &node->type : NULL;
  }
  if (ok && (ffi_get_struct_offsets(FFI_DEFAULT_ABI, &node->type, offsets) != FFI_OK ||
             node->type.size != size || node->type.alignment != alignment)) {
    oti_error(rt, "FFI abi: aggregate layout mismatch");
    ok = false;
  }
  if (ok && !element) {
    cursor = members;
    for (size_t i = 0; i < n; i++, cursor = object_fields(cursor)[1]) {
      size_t offset;
      if (!natural_size(list_item(object_fields(cursor)[0], 2), &offset) || offset != offsets[i]) {
        oti_error(rt, "FFI abi: field offset mismatch");
        ok = false;
        break;
      }
    }
  }
  free(offsets);
  return ok ? &node->type : NULL;
}

static bool same_abi_type(const ffi_type* a, const ffi_type* b) {
  if (a == b) return true;
  if (a->type != FFI_TYPE_STRUCT || b->type != FFI_TYPE_STRUCT || a->size != b->size ||
      a->alignment != b->alignment)
    return false;
  size_t i = 0;
  while (a->elements[i] && b->elements[i]) {
    if (!same_abi_type(a->elements[i], b->elements[i])) return false;
    i++;
  }
  return !a->elements[i] && !b->elements[i];
}

static bool call_type(ot_state* rt, oti_signature* s, ot_value v, bool codes, unsigned* id,
                      ffi_type** abi) {
  if (codes && oti_kind(v, K_PAIR)) {
    size_t budget = 65536;
    *id = AGGREGATE;
    *abi = aggregate_type(rt, s, v, 0, &budget);
    return *abi != NULL;
  }
  int type = codes ? oti_foreign_scalar_code(rt, v, false) : oti_foreign_scalar(rt, v, false);
  if (type < 0) return false;
  *id = (unsigned)type;
  *abi = abi_type(*id);
  return true;
}

oti_signature* oti_foreign_signature(ot_state* rt, oti_signature** cache, ot_value arguments,
                                     ot_value result, bool codes) {
  oti_signature wanted = {0};
  if (!oti_foreign_list_length(rt, arguments, MAX_ARGUMENTS, &wanted.count)) return 0;
  bool valid = call_type(rt, &wanted, result, codes, &wanted.result, &wanted.result_type);
  ot_value list = arguments;
  for (size_t i = 0; valid && i < wanted.count; i++, list = object_fields(list)[1]) {
    valid = call_type(rt, &wanted, object_fields(list)[0], codes, &wanted.arguments[i],
                      &wanted.argument_types[i]);
    if (valid && wanted.arguments[i] == 0) valid = oti_error(rt, "FFI signature: void argument");
  }
  if (!valid) {
    oti_foreign_free_abi_nodes(wanted.nodes);
    return 0;
  }
  oti_signature* s;
  for (s = *cache; s; s = s->next) {
    if (s->count != wanted.count || s->result != wanted.result ||
        memcmp(s->arguments, wanted.arguments, s->count * sizeof(unsigned)) ||
        !same_abi_type(s->result_type, wanted.result_type))
      continue;
    size_t i = 0;
    while (i < s->count && same_abi_type(s->argument_types[i], wanted.argument_types[i])) i++;
    if (i == s->count) break;
  }
  if (s) oti_foreign_free_abi_nodes(wanted.nodes);
  if (s && s != *cache) {
    oti_signature** link = cache;
    while (*link != s) link = &(*link)->next;
    *link = s->next;
    s->next = *cache;
    *cache = s;
  }
  if (!s) {
    s = malloc(sizeof(*s));
    if (!s) {
      oti_foreign_free_abi_nodes(wanted.nodes);
      oti_error(rt, "FFI allocation: out of host memory");
      return NULL;
    }
    *s = wanted;
    if (ffi_prep_cif(&s->cif, FFI_DEFAULT_ABI, (unsigned)s->count, s->result_type,
                     s->argument_types) != FFI_OK) {
      oti_foreign_free_abi_nodes(s->nodes);
      free(s);
      oti_error(rt, "FFI signature: unsupported ABI");
      return NULL;
    }
    s->next = *cache;
    *cache = s;
  }
  return s;
}
