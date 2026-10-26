#include "foreign_types.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define SIGNED(n, t, lo, hi) {n, #t, "signed_integer", T_SIGNED, sizeof(t), _Alignof(t), lo, hi}
#define UNSIGNED(n, t, hi) {n, #t, "unsigned_integer", T_UNSIGNED, sizeof(t), _Alignof(t), 0, hi}
const Scalar foreign_scalars[AGGREGATE] = {
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
    {"callback-pointer", "ForeignCallback", "callback", T_CALLBACK, sizeof(ForeignCallback),
     _Alignof(ForeignCallback), 0, 0},
    {"utf8", "const char *", "pointer", T_UTF8, sizeof(char*), _Alignof(char*), 0, 0}};
#undef SIGNED
#undef UNSIGNED

int foreign_scalar(Runtime* rt, Value name, bool memory) {
  if (rt_kind(name, K_SYMBOL)) {
    size_t length;
    memcpy(&length, object_bytes(name), sizeof(length));
    const char* s = (const char*)object_bytes(name) + sizeof(length);
    for (size_t i = 0; i < sizeof(foreign_scalars) / sizeof(foreign_scalars[0]); i++)
      if (strlen(foreign_scalars[i].name) == length &&
          !memcmp(foreign_scalars[i].name, s, length)) {
        if (memory && (foreign_scalars[i].kind == T_VOID || foreign_scalars[i].kind == T_UTF8))
          break;
        return (int)i;
      }
  }
  rt_error(rt, "FFI type: unsupported scalar type");
  return -1;
}

bool foreign_list_length(Runtime* rt, Value list, size_t limit, size_t* count) {
  *count = 0;
  while (rt_kind(list, K_PAIR) && *count < limit) {
    (*count)++;
    list = object_fields(list)[1];
  }
  return list == V_EMPTY || rt_error(rt, "FFI signature: improper, cyclic or oversized list");
}

void foreign_free_abi_nodes(AbiNode* node) {
  while (node) {
    AbiNode* next = node->next;
    free(node->type.elements);
    free(node);
    node = next;
  }
}

static ffi_type* abi_type(unsigned id) {
  const Scalar* s = &foreign_scalars[id];
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

int foreign_scalar_code(Runtime* rt, Value value, bool memory) {
  if (value_is_int(value) && value_to_int(value) >= 0 &&
      (size_t)value_to_int(value) < sizeof(foreign_scalars) / sizeof(*foreign_scalars)) {
    int code = (int)value_to_int(value);
    if (!memory || (foreign_scalars[code].kind != T_VOID && foreign_scalars[code].kind != T_UTF8))
      return code;
  }
  rt_error(rt, "FFI type: invalid scalar code");
  return -1;
}

static bool natural_size(Value v, size_t* out) {
  if (!value_is_int(v) || value_to_int(v) < 0) return false;
  *out = (size_t)value_to_int(v);
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

static bool lower_union(Runtime* rt, Signature* s, AbiNode* node, size_t n,
                        size_t size, size_t alignment) {
  size_t largest = 0, aligned = 1;
  for (size_t i = 0; i < n; i++) {
    ffi_type* member = node->type.elements[i];
    if (member->size > largest) largest = member->size;
    if (member->alignment > aligned) aligned = member->alignment;
  }
  if (aligned != alignment || largest > SIZE_MAX - alignment + 1 ||
      (largest + alignment - 1) / alignment * alignment != size)
    return rt_error(rt, "FFI abi: union layout mismatch");
  unsigned char* classes = calloc(size, 1);
  ffi_type** elements = calloc(size + 1, sizeof(*elements));
  AbiNode* padding = calloc(1, sizeof(*padding));
  if (!classes || !elements || !padding) {
    free(classes);
    free(elements);
    free(padding);
    return rt_error(rt, "out of host memory");
  }
  padding->type = (ffi_type){1, 1, FFI_TYPE_STRUCT, calloc(1, sizeof(ffi_type*))};
  if (!padding->type.elements) {
    free(classes);
    free(elements);
    free(padding);
    return rt_error(rt, "out of host memory");
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
static ffi_type* aggregate_type(Runtime* rt, Signature* s, Value description, unsigned depth,
                                size_t* budget) {
  size_t n, kind, size, alignment;
  if (depth > 64 || !*budget) {
    rt_error(rt, "FFI signature: aggregate too complex");
    return NULL;
  }
  --*budget;
  if (!foreign_list_length(rt, description, 4, &n) || n != 4 ||
      !natural_size(list_item(description, 0), &kind) ||
      !natural_size(list_item(description, 1), &size) ||
      !natural_size(list_item(description, 2), &alignment)) {
    rt_error(rt, "FFI signature: invalid type description");
    return NULL;
  }
  if (kind < AGGREGATE) {
    if (!kind || kind == 22 || size != foreign_scalars[kind].size ||
        alignment != foreign_scalars[kind].alignment) {
      rt_error(rt, "FFI signature: invalid aggregate member");
      return NULL;
    }
    return abi_type((unsigned)kind);
  }
  if ((kind != AGGREGATE && kind != ARRAY_TYPE && kind != UNION_TYPE) || !size || !alignment ||
      alignment > _Alignof(max_align_t)) {
    rt_error(rt, "FFI signature: invalid aggregate layout");
    return NULL;
  }
  if (size > 65536) {
    rt_error(rt, "FFI signature: aggregate too complex");
    return NULL;
  }
  Value members = list_item(description, 3);
  ffi_type* element = NULL;
  if (kind == ARRAY_TYPE) {
    size_t entries;
    if (!foreign_list_length(rt, members, 2, &entries) || entries != 2 ||
        !natural_size(list_item(members, 1), &n)) {
      rt_error(rt, "FFI signature: invalid array layout");
      return NULL;
    }
    element = aggregate_type(rt, s, list_item(members, 0), depth + 1, budget);
    if (!element) return NULL;
  } else if (!foreign_list_length(rt, members, *budget, &n)) return NULL;
  if (!n || n > *budget) {
    rt_error(rt, "FFI signature: empty or oversized aggregate");
    return NULL;
  }
  *budget -= n;
  AbiNode* node = calloc(1, sizeof(*node));
  if (!node) {
    rt_error(rt, "out of host memory");
    return NULL;
  }
  node->next = s->nodes;
  s->nodes = node;
  node->type.type = FFI_TYPE_STRUCT;
  node->type.elements = calloc(n + 1, sizeof(ffi_type*));
  size_t* offsets = calloc(n, sizeof(size_t));
  if (!node->type.elements || !offsets) {
    free(offsets);
    rt_error(rt, "out of host memory");
    return NULL;
  }
  Value cursor = members;
  bool ok = true;
  for (size_t i = 0; ok && i < n; i++) {
    if (element) node->type.elements[i] = element;
    else {
      Value field = object_fields(cursor)[0];
      size_t count;
      if (!foreign_list_length(rt, field, 3, &count) || count != 3 ||
          !rt_kind(list_item(field, 0), K_SYMBOL)) {
        rt_error(rt, "FFI signature: invalid aggregate field");
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
        ok = rt_error(rt, "FFI abi: union field offset mismatch");
    }
#if defined(__x86_64__) && defined(__linux__) && !defined(__ILP32__)
    if (ok) ok = lower_union(rt, s, node, n, size, alignment);
#else
    if (ok) ok = rt_error(rt, "FFI abi: union calls require x86-64 Linux SysV");
#endif
    free(offsets);
    return ok ? &node->type : NULL;
  }
  if (ok && (ffi_get_struct_offsets(FFI_DEFAULT_ABI, &node->type, offsets) != FFI_OK ||
             node->type.size != size || node->type.alignment != alignment)) {
    rt_error(rt, "FFI abi: aggregate layout mismatch");
    ok = false;
  }
  if (ok && !element) {
    cursor = members;
    for (size_t i = 0; i < n; i++, cursor = object_fields(cursor)[1]) {
      size_t offset;
      if (!natural_size(list_item(object_fields(cursor)[0], 2), &offset) || offset != offsets[i]) {
        rt_error(rt, "FFI abi: field offset mismatch");
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

static bool call_type(Runtime* rt, Signature* s, Value v, bool codes, unsigned* id,
                      ffi_type** abi) {
  if (codes && rt_kind(v, K_PAIR)) {
    size_t budget = 65536;
    *id = AGGREGATE;
    *abi = aggregate_type(rt, s, v, 0, &budget);
    return *abi != NULL;
  }
  int type = codes ? foreign_scalar_code(rt, v, false) : foreign_scalar(rt, v, false);
  if (type < 0) return false;
  *id = (unsigned)type;
  *abi = abi_type(*id);
  return true;
}

Signature* foreign_signature(Runtime* rt, Signature** cache, Value arguments, Value result,
                             bool codes) {
  Signature wanted = {0};
  if (!foreign_list_length(rt, arguments, MAX_ARGUMENTS, &wanted.count)) return 0;
  bool valid = call_type(rt, &wanted, result, codes, &wanted.result, &wanted.result_type);
  Value list = arguments;
  for (size_t i = 0; valid && i < wanted.count; i++, list = object_fields(list)[1]) {
    valid = call_type(rt, &wanted, object_fields(list)[0], codes, &wanted.arguments[i],
                      &wanted.argument_types[i]);
    if (valid && wanted.arguments[i] == 0) valid = rt_error(rt, "FFI signature: void argument");
  }
  if (!valid) {
    foreign_free_abi_nodes(wanted.nodes);
    return 0;
  }
  Signature* s;
  for (s = *cache; s; s = s->next) {
    if (s->count != wanted.count || s->result != wanted.result ||
        memcmp(s->arguments, wanted.arguments, s->count * sizeof(unsigned)) ||
        !same_abi_type(s->result_type, wanted.result_type))
      continue;
    size_t i = 0;
    while (i < s->count && same_abi_type(s->argument_types[i], wanted.argument_types[i])) i++;
    if (i == s->count) break;
  }
  if (s) foreign_free_abi_nodes(wanted.nodes);
  if (s && s != *cache) {
    Signature** link = cache;
    while (*link != s) link = &(*link)->next;
    *link = s->next;
    s->next = *cache;
    *cache = s;
  }
  if (!s) {
    s = malloc(sizeof(*s));
    if (!s) {
      foreign_free_abi_nodes(wanted.nodes);
      rt_error(rt, "out of host memory");
      return NULL;
    }
    *s = wanted;
    if (ffi_prep_cif(&s->cif, FFI_DEFAULT_ABI, (unsigned)s->count, s->result_type,
                     s->argument_types) != FFI_OK) {
      foreign_free_abi_nodes(s->nodes);
      free(s);
      rt_error(rt, "FFI signature: unsupported ABI");
      return NULL;
    }
    s->next = *cache;
    *cache = s;
  }
  return s;
}
