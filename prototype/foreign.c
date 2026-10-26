#include "internal.h"
#include "foreign.h"
#include <dlfcn.h>
#include <ffi.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { T_VOID, T_BOOL, T_SIGNED, T_UNSIGNED, T_FLOAT, T_DOUBLE, T_POINTER, T_CALLBACK, T_UTF8 };
typedef struct {
    const char *name, *c_type, *member;
    unsigned kind;
    size_t size, alignment;
    int64_t minimum;
    uint64_t maximum;
} Scalar;
#define SIGNED(n,t,lo,hi) {n,#t,"signed_integer",T_SIGNED,sizeof(t),_Alignof(t),lo,hi}
#define UNSIGNED(n,t,hi) {n,#t,"unsigned_integer",T_UNSIGNED,sizeof(t),_Alignof(t),0,hi}
static const Scalar scalars[] = {
    {"void","void","",T_VOID,0,1,0,0},
    {"bool","bool","boolean",T_BOOL,sizeof(bool),_Alignof(bool),0,0},
    SIGNED("i8",int8_t,INT8_MIN,INT8_MAX), UNSIGNED("u8",uint8_t,UINT8_MAX),
    SIGNED("i16",int16_t,INT16_MIN,INT16_MAX), UNSIGNED("u16",uint16_t,UINT16_MAX),
    SIGNED("i32",int32_t,INT32_MIN,INT32_MAX), UNSIGNED("u32",uint32_t,UINT32_MAX),
    SIGNED("i64",int64_t,INT64_MIN,INT64_MAX), UNSIGNED("u64",uint64_t,UINT64_MAX),
    SIGNED("short",short,SHRT_MIN,SHRT_MAX), UNSIGNED("ushort",unsigned short,USHRT_MAX),
    SIGNED("int",int,INT_MIN,INT_MAX), UNSIGNED("uint",unsigned int,UINT_MAX),
    SIGNED("long",long,LONG_MIN,LONG_MAX), UNSIGNED("ulong",unsigned long,ULONG_MAX),
    UNSIGNED("size-t",size_t,SIZE_MAX), SIGNED("ptrdiff-t",ptrdiff_t,PTRDIFF_MIN,PTRDIFF_MAX),
    {"float","float","single",T_FLOAT,sizeof(float),_Alignof(float),0,0},
    {"double","double","real",T_DOUBLE,sizeof(double),_Alignof(double),0,0},
    {"pointer","void *","pointer",T_POINTER,sizeof(void *),_Alignof(void *),0,0},
    {"callback-pointer","ForeignCallback","callback",T_CALLBACK,sizeof(ForeignCallback),_Alignof(ForeignCallback),0,0},
    {"utf8","const char *","pointer",T_UTF8,sizeof(char *),_Alignof(char *),0,0}
};
#undef SIGNED
#undef UNSIGNED
#define MAX_ARGUMENTS 64

typedef struct Library Library;
struct Library { Library *next; void *handle; };
enum { AGGREGATE = 23, UNION_TYPE = 24, ARRAY_TYPE = 25 };
typedef struct AbiNode AbiNode;
struct AbiNode { AbiNode *next; ffi_type type; };
typedef struct Signature Signature;
struct Signature {
    Signature *next;
    size_t count;
    unsigned arguments[MAX_ARGUMENTS], result;
    ffi_cif cif;
    ffi_type *argument_types[MAX_ARGUMENTS], *result_type;
    AbiNode *nodes;
    bool marked, pending;
};
typedef struct Binding Binding;
struct Binding { Binding *next; Signature *signature; void *target; bool collect_safe, marked; };
typedef struct Memory Memory;
struct Memory { Memory *next; void *allocation; uintptr_t address; size_t size; bool released, marked; };
typedef struct {
    uintptr_t address;
    Memory *owner;
    size_t length;
    bool bounded, view;
} Pointer;
struct ForeignState {
    Library *libraries;
    Signature *signatures;
    Binding *bindings;
    Memory *memory;
    size_t allocations, bytes;
    uint32_t null_constant;
    char loader_error[512];
};

size_t runtime_foreign_signatures(const Runtime *rt) {
    size_t count = 0;
    for (Signature *s = rt->foreign->signatures; s; s = s->next) count++;
    return count;
}


static Value failure(Runtime *rt, const char *message) { rt_error(rt, message); return 0; }

static const char *text(Value value, size_t *length) {
    unsigned char *bytes = object_bytes(value);
    memcpy(length, bytes, sizeof(*length));
    return (const char *)bytes + sizeof(*length);
}

static const char *c_string(Runtime *rt, Value value) {
    if (!rt_kind(value, K_STRING)) { rt_error(rt, "FFI type: expected string"); return NULL; }
    size_t length;
    const char *s = text(value, &length);
    if (memchr(s, 0, length)) { rt_error(rt, "FFI encoding: embedded NUL"); return NULL; }
    return s;
}

static bool utf8(const unsigned char *s, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned char c = s[i++];
        if (c < 0x80) continue;
        unsigned more, minimum;
        uint32_t code;
        if (c >= 0xc2 && c <= 0xdf) { more = 1; minimum = 0x80; code = c & 31; }
        else if (c >= 0xe0 && c <= 0xef) { more = 2; minimum = 0x800; code = c & 15; }
        else if (c >= 0xf0 && c <= 0xf4) { more = 3; minimum = 0x10000; code = c & 7; }
        else return false;
        if (more > n - i) return false;
        while (more--) {
            c = s[i++];
            if ((c & 0xc0) != 0x80) return false;
            code = (code << 6) | (c & 63);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff)) return false;
    }
    return true;
}

static int scalar(Runtime *rt, Value name, bool memory) {
    if (rt_kind(name, K_SYMBOL)) {
        size_t length;
        const char *s = text(name, &length);
        for (size_t i = 0; i < sizeof(scalars) / sizeof(scalars[0]); i++)
            if (strlen(scalars[i].name) == length && !memcmp(scalars[i].name, s, length)) {
                if (memory && (scalars[i].kind == T_VOID || scalars[i].kind == T_UTF8)) break;
                return (int)i;
            }
    }
    rt_error(rt, "FFI type: unsupported scalar type");
    return -1;
}

static bool length(Runtime *rt, Value list, size_t limit, size_t *count) {
    *count = 0;
    while (rt_kind(list, K_PAIR) && *count < limit) {
        (*count)++;
        list = object_fields(list)[1];
    }
    return list == V_EMPTY || rt_error(rt, "FFI signature: improper, cyclic or oversized list");
}

static bool pointer(Runtime *rt, Value value, Pointer *out) {
    *out = (Pointer){0};
    if (!rt_kind(value, K_POINTER)) return rt_error(rt, "FFI type: expected pointer");
    memcpy(out, object_bytes(value), sizeof(*out));
    if (out->owner && out->owner->released) return rt_error(rt, "FFI released pointer");
    return true;
}

static Value pointer_object(Runtime *rt, Pointer p) {
    if (!p.address) return rt->constants[rt->foreign->null_constant];
    Value value = rt_alloc(rt, K_POINTER, 0, sizeof(p));
    if (value) memcpy(object_bytes(value), &p, sizeof(p));
    return value;
}

static Value wrap_pointer(Runtime *rt, void *address) {
    Pointer p = {.address = (uintptr_t)address};
    for (Memory *m = rt->foreign->memory; address && m; m = m->next) {
        if (!m->released && p.address >= m->address && p.address - m->address <= m->size) {
            p.owner = m; p.length = m->size - (p.address - m->address); p.bounded = true; p.view = true;
            break;
        }
    }
    return pointer_object(rt, p);
}

static Value library_object(Runtime *rt, Library *library, Value name) {
    Roots roots; roots_push(&rt->heap, &roots, &name, 1);
    Value value = rt_alloc(rt, K_FOREIGN_LIBRARY, 1, sizeof(library));
    if (value) {
        memcpy(object_bytes(value), &library, sizeof(library));
        object_fields(value)[0] = name;
    }
    roots_pop(&rt->heap, &roots); return value;
}

static void *object_data(Value value) {
    void *data;
    memcpy(&data, object_bytes(value), sizeof(data));
    return data;
}

RuntimeForeignStats runtime_foreign_stats(const Runtime *rt) {
    RuntimeForeignStats stats = {0};
    const ForeignState *state = rt->foreign;
    if (!state) return stats;
    for (Library *p = state->libraries; p; p = p->next) stats.libraries++;
    for (Binding *p = state->bindings; p; p = p->next) stats.bindings++;
    for (Signature *p = state->signatures; p; p = p->next) stats.signatures++;
    for (Memory *p = state->memory; p; p = p->next) stats.memory_records++;
    stats.allocations = state->allocations;
    stats.bytes = state->bytes;
    return stats;
}

static void free_abi_nodes(AbiNode *node) {
    while (node) { AbiNode *next = node->next; free(node->type.elements); free(node); node = next; }
}

void rt_foreign_collect(Runtime *rt) {
    ForeignState *state = rt->foreign;
    if (!state || (!state->bindings && !state->signatures && !state->memory)) return;
    for (Binding *p = state->bindings; p; p = p->next) p->marked = false;
    for (Signature *p = state->signatures; p; p = p->next) p->marked = false;
    for (Memory *p = state->memory; p; p = p->next) p->marked = false;
    unsigned char *space = rt->heap.space[rt->heap.active];
    for (size_t offset = 0; offset < rt->heap.used;) {
        Value value = (Value)(space + offset);
        if (rt_kind(value, K_FOREIGN_FUNCTION)) {
            Binding *binding = object_data(value);
            if (binding) { binding->marked = true; binding->signature->marked = true; }
        } else if (rt_kind(value, K_POINTER)) {
            Pointer pointer;
            memcpy(&pointer, object_bytes(value), sizeof(pointer));
            if (pointer.owner) pointer.owner->marked = true;
        }
        offset += ((HeapObject *)value)->size;
    }
    Binding **binding = &state->bindings;
    while (*binding) {
        Binding *p = *binding;
        if (p->marked) binding = &p->next;
        else { *binding = p->next; free(p); }
    }
    /* Keep four recently bound, unused call interfaces to avoid preparing common
     * signatures during reload. Live bindings and in-progress wrappers pin theirs. */
    size_t spare = 0;
    Signature **signature = &state->signatures;
    while (*signature) {
        Signature *p = *signature;
        if (p->marked || p->pending || spare++ < 4) signature = &p->next;
        else {
            *signature = p->next;
            free_abi_nodes(p->nodes); free(p);
        }
    }
    Memory **memory = &state->memory;
    while (*memory) {
        Memory *p = *memory;
        /* C may retain an allocation after all Otium wrappers disappear. */
        if (p->marked || !p->released) memory = &p->next;
        else { *memory = p->next; free(p); }
    }
}

bool rt_foreign_init(Runtime *rt) {
    rt->foreign = calloc(1, sizeof(*rt->foreign));
    if (!rt->foreign) return rt_error(rt, "out of host memory");
    Value null = rt_alloc(rt, K_POINTER, 0, sizeof(Pointer));
    if (!null) return false;
    uint32_t index = rt_constant(rt, null);
    if (index == UINT32_MAX) return false;
    rt->foreign->null_constant = index;
    uint32_t global = rt_resolve_global(rt, "ffi/null", 8);
    if (global == UINT32_MAX) return false;
    rt->globals[global] = rt->constants[index];
    return true;
}

void rt_foreign_destroy(Runtime *rt) {
    ForeignState *state = rt->foreign;
    if (!state) return;
    while (state->bindings) { Binding *next = state->bindings->next; free(state->bindings); state->bindings = next; }
    while (state->signatures) {
        Signature *next = state->signatures->next;
        free_abi_nodes(state->signatures->nodes); free(state->signatures); state->signatures = next;
    }
    while (state->libraries) {
        Library *next = state->libraries->next;
        dlclose(state->libraries->handle); free(state->libraries); state->libraries = next;
    }
    while (state->memory) {
        Memory *next = state->memory->next;
        free(state->memory->allocation); free(state->memory); state->memory = next;
    }
    free(state); rt->foreign = NULL;
}

static Value open_library(Runtime *rt, Value names) {
    size_t count;
    if (!length(rt, names, 1024, &count)) return 0;
    if (!count) return failure(rt, "FFI load: expected nonempty library list");
    for (Value p = names; p != V_EMPTY; p = object_fields(p)[1]) {
        const char *name = c_string(rt, object_fields(p)[0]);
        if (!name) return 0;
        void *handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
        if (!handle) continue;
        for (Library *existing = rt->foreign->libraries; existing; existing = existing->next) {
            if (existing->handle != handle) continue;
            dlclose(handle);
            return library_object(rt, existing, object_fields(p)[0]);
        }
        Library *library = calloc(1, sizeof(*library));
        if (!library) { dlclose(handle); return failure(rt, "out of host memory"); }
        library->handle = handle;
        Value v = library_object(rt, library, object_fields(p)[0]);
        if (!v) { dlclose(handle); free(library); return 0; }
        library->next = rt->foreign->libraries; rt->foreign->libraries = library;
        return v;
    }
    return failure(rt, "FFI load: could not open a candidate library");
}

static ffi_type *abi_type(unsigned id) {
    const Scalar *s = &scalars[id];
    switch (s->kind) {
    case T_VOID: return &ffi_type_void;
    case T_BOOL: return &ffi_type_uint8;
    case T_FLOAT: return &ffi_type_float;
    case T_DOUBLE: return &ffi_type_double;
    case T_POINTER: case T_CALLBACK: case T_UTF8: return &ffi_type_pointer;
    case T_SIGNED:
        switch (s->size) {
        case 1: return &ffi_type_sint8; case 2: return &ffi_type_sint16;
        case 4: return &ffi_type_sint32; case 8: return &ffi_type_sint64;
        }
        break;
    case T_UNSIGNED:
        switch (s->size) {
        case 1: return &ffi_type_uint8; case 2: return &ffi_type_uint16;
        case 4: return &ffi_type_uint32; case 8: return &ffi_type_uint64;
        }
    }
    return NULL;
}

static void memory_scalar(unsigned type, void *address, ForeignSlot *value, bool write);

static int scalar_code(Runtime *rt,Value value,bool memory) {
    if(value_is_int(value) && value_to_int(value)>=0 && (size_t)value_to_int(value)<sizeof(scalars)/sizeof(*scalars)) {
        int code=(int)value_to_int(value);
        if(!memory || (scalars[code].kind!=T_VOID && scalars[code].kind!=T_UTF8)) return code;
    }
    rt_error(rt,"FFI type: invalid scalar code"); return -1;
}
static bool natural_size(Value v, size_t *out) {
    if (!value_is_int(v) || value_to_int(v) < 0) return false;
    *out = (size_t)value_to_int(v); return true;
}
static Value list_item(Value list, size_t index) {
    while (index--) list = object_fields(list)[1];
    return object_fields(list)[0];
}

/* This builds host-owned ABI trees without allocating in the moving heap. */
static ffi_type *aggregate_type(Runtime *rt, Signature *s, Value description, unsigned depth, size_t *budget) {
    size_t n, kind, size, alignment;
    if (depth > 64 || !*budget) { rt_error(rt, "FFI signature: aggregate too complex"); return NULL; }
    --*budget;
    if (!length(rt, description, 4, &n) || n != 4 ||
        !natural_size(list_item(description, 0), &kind) ||
        !natural_size(list_item(description, 1), &size) ||
        !natural_size(list_item(description, 2), &alignment)) {
        rt_error(rt, "FFI signature: invalid type description"); return NULL;
    }
    if (kind < AGGREGATE) {
        if (!kind || kind == 22 || size != scalars[kind].size || alignment != scalars[kind].alignment) {
            rt_error(rt, "FFI signature: invalid aggregate member"); return NULL;
        }
        return abi_type((unsigned)kind);
    }
    if (kind == UNION_TYPE) { rt_error(rt, "FFI abi: union classification not implemented"); return NULL; }
    if ((kind != AGGREGATE && kind != ARRAY_TYPE) || !size || !alignment || alignment > _Alignof(max_align_t)) {
        rt_error(rt, "FFI signature: invalid aggregate layout"); return NULL;
    }
    Value members = list_item(description, 3);
    ffi_type *element = NULL;
    if (kind == ARRAY_TYPE) {
        size_t entries;
        if (!length(rt, members, 2, &entries) || entries != 2 || !natural_size(list_item(members, 1), &n)) {
            rt_error(rt, "FFI signature: invalid array layout"); return NULL;
        }
        element = aggregate_type(rt, s, list_item(members, 0), depth + 1, budget);
        if (!element) return NULL;
    } else if (!length(rt, members, *budget, &n)) return NULL;
    if (!n || n > *budget) { rt_error(rt, "FFI signature: empty or oversized aggregate"); return NULL; }
    *budget -= n;
    AbiNode *node = calloc(1, sizeof(*node));
    if (!node) { rt_error(rt, "out of host memory"); return NULL; }
    node->next = s->nodes; s->nodes = node;
    node->type.type = FFI_TYPE_STRUCT;
    node->type.elements = calloc(n + 1, sizeof(ffi_type *));
    size_t *offsets = calloc(n, sizeof(size_t));
    if (!node->type.elements || !offsets) { free(offsets); rt_error(rt, "out of host memory"); return NULL; }
    Value cursor = members;
    bool ok = true;
    for (size_t i = 0; ok && i < n; i++) {
        if (element) node->type.elements[i] = element;
        else {
            Value field = object_fields(cursor)[0]; size_t count;
            if (!length(rt, field, 3, &count) || count != 3 || !rt_kind(list_item(field, 0), K_SYMBOL)) {
                rt_error(rt, "FFI signature: invalid aggregate field"); ok = false; break;
            }
            node->type.elements[i] = aggregate_type(rt, s, list_item(field, 1), depth + 1, budget);
            ok = node->type.elements[i] != NULL;
            cursor = object_fields(cursor)[1];
        }
    }
    if (ok && (ffi_get_struct_offsets(FFI_DEFAULT_ABI, &node->type, offsets) != FFI_OK ||
        node->type.size != size || node->type.alignment != alignment)) {
        rt_error(rt, "FFI abi: aggregate layout mismatch"); ok = false;
    }
    if (ok && !element) {
        cursor = members;
        for (size_t i = 0; i < n; i++, cursor = object_fields(cursor)[1]) {
            size_t offset;
            if (!natural_size(list_item(object_fields(cursor)[0], 2), &offset) || offset != offsets[i]) {
                rt_error(rt, "FFI abi: field offset mismatch"); ok = false; break;
            }
        }
    }
    free(offsets); return ok ? &node->type : NULL;
}
static bool same_abi_type(const ffi_type *a, const ffi_type *b) {
    if (a == b) return true;
    if (a->type != FFI_TYPE_STRUCT || b->type != FFI_TYPE_STRUCT ||
        a->size != b->size || a->alignment != b->alignment) return false;
    size_t i = 0;
    while (a->elements[i] && b->elements[i]) {
        if (!same_abi_type(a->elements[i], b->elements[i])) return false;
        i++;
    }
    return !a->elements[i] && !b->elements[i];
}
static bool call_type(Runtime *rt, Signature *s, Value v, bool codes, unsigned *id, ffi_type **abi) {
    if (codes && rt_kind(v, K_PAIR)) {
        size_t budget = 65536;
        *id = AGGREGATE; *abi = aggregate_type(rt, s, v, 0, &budget);
        return *abi != NULL;
    }
    int type = codes ? scalar_code(rt, v, false) : scalar(rt, v, false);
    if (type < 0) return false;
    *id = (unsigned)type; *abi = abi_type(*id); return true;
}
static Value layout_copy(Runtime *rt, Value value, unsigned depth, size_t *budget);

static Value bind_function(Runtime *rt, size_t first, bool codes) {
    Value *args = rt->stack + first;
    if (!rt_kind(args[0], K_FOREIGN_LIBRARY)) return failure(rt, "FFI type: expected library");
    const char *name = c_string(rt, args[1]);
    if (!name) return 0;
    if (args[4] != V_TRUE && args[4] != V_FALSE) return failure(rt, "FFI type: expected collection safety boolean");
    Library *library = object_data(args[0]);
    dlerror();
    void *target = dlsym(library->handle, name);
    if (dlerror() || !target) return failure(rt, "FFI symbol: symbol not found");
    Signature wanted = {0};
    if (!length(rt, args[2], MAX_ARGUMENTS, &wanted.count)) return 0;
    bool valid = call_type(rt, &wanted, args[3], codes, &wanted.result, &wanted.result_type);
    Value list = args[2];
    for (size_t i = 0; valid && i < wanted.count; i++, list = object_fields(list)[1]) {
        valid = call_type(rt, &wanted, object_fields(list)[0], codes, &wanted.arguments[i], &wanted.argument_types[i]);
        if (valid && wanted.arguments[i] == 0) valid = rt_error(rt, "FFI signature: void argument");
    }
    if (!valid) { free_abi_nodes(wanted.nodes); return 0; }
    Signature *s;
    for (s = rt->foreign->signatures; s; s = s->next) {
        if (s->count != wanted.count || s->result != wanted.result ||
            memcmp(s->arguments, wanted.arguments, s->count * sizeof(unsigned)) ||
            !same_abi_type(s->result_type, wanted.result_type)) continue;
        size_t i = 0;
        while (i < s->count && same_abi_type(s->argument_types[i], wanted.argument_types[i])) i++;
        if (i == s->count) break;
    }
    if (s) free_abi_nodes(wanted.nodes);
    if (s && s != rt->foreign->signatures) {
        Signature **link = &rt->foreign->signatures;
        while (*link != s) link = &(*link)->next;
        *link = s->next;
        s->next = rt->foreign->signatures;
        rt->foreign->signatures = s;
    }
    if (!s) {
        s = malloc(sizeof(*s));
        if (!s) { free_abi_nodes(wanted.nodes); return failure(rt, "out of host memory"); }
        *s = wanted;
        if (ffi_prep_cif(&s->cif, FFI_DEFAULT_ABI, (unsigned)s->count,
                s->result_type, s->argument_types) != FFI_OK) {
            free_abi_nodes(s->nodes); free(s); return failure(rt, "FFI signature: unsupported ABI");
        }
        s->next = rt->foreign->signatures; rt->foreign->signatures = s;
    }
    Binding *b = malloc(sizeof(*b));
    if (!b) return failure(rt, "out of host memory");
    *b = (Binding){rt->foreign->bindings, s, target, args[4] == V_TRUE, false};
    s->pending = true;
    Value live[] = {V_NIL, V_NIL}; Roots roots;
    roots_push(&rt->heap, &roots, live, 2);
    size_t budget = 65536;
    live[0] = layout_copy(rt, rt->stack[first + 2], 0, &budget);
    budget = 65536;
    if (live[0]) live[1] = layout_copy(rt, rt->stack[first + 3], 0, &budget);
    Value value = live[0] && live[1] ? rt_alloc(rt, K_FOREIGN_FUNCTION, 4, sizeof(b)) : 0;
    if (value) {
        memcpy(object_bytes(value), &b, sizeof(b));
        object_fields(value)[0] = rt->stack[first];
        object_fields(value)[1] = rt->stack[first + 1];
        object_fields(value)[2] = live[0];
        object_fields(value)[3] = live[1];
    }
    roots_pop(&rt->heap, &roots);
    s->pending = false;
    if (!value) { free(b); return 0; }
    b->next = rt->foreign->bindings;
    rt->foreign->bindings = b;
    return value;
}

static bool to_foreign(Runtime *rt, const Scalar *type, Value value, ForeignSlot *slot) {
    switch (type->kind) {
    case T_BOOL:
        if (value != V_TRUE && value != V_FALSE) return rt_error(rt, "FFI type: expected boolean");
        slot->boolean = value == V_TRUE; return true;
    case T_SIGNED: case T_UNSIGNED: {
        if (!value_is_int(value)) return rt_error(rt, "FFI type: expected exact integer");
        int64_t n = value_to_int(value);
        if (n < type->minimum || (n >= 0 && (uint64_t)n > type->maximum))
            return rt_error(rt, "FFI range: integer argument out of range");
        if (type->kind == T_SIGNED) slot->signed_integer = n;
        else slot->unsigned_integer = (uint64_t)n;
        return true;
    }
    case T_FLOAT: case T_DOUBLE: {
        double d;
        if (value_is_int(value)) d = (double)value_to_int(value);
        else if (rt_kind(value, K_FLOAT)) memcpy(&d, object_bytes(value), sizeof(d));
        else return rt_error(rt, "FFI type: expected number");
        if (type->kind == T_FLOAT) {
            if (isfinite(d) && fabs(d) > FLT_MAX) return rt_error(rt, "FFI range: float argument out of range");
            slot->single = (float)d;
        } else slot->real = d;
        return true;
    }
    case T_POINTER: case T_CALLBACK: {
        Pointer p;
        if (!pointer(rt, value, &p)) return false;
        if (type->kind == T_CALLBACK) {
            if (p.address) return rt_error(rt, "FFI type: callbacks not implemented");
            slot->callback = NULL;
        } else slot->pointer = (void *)p.address;
        return true;
    }
    default: return rt_error(rt, "FFI type: unsupported conversion");
    }
}

static Value from_foreign(Runtime *rt, const Scalar *type, ForeignSlot value) {
    switch (type->kind) {
    case T_VOID: return V_FALSE;
    case T_BOOL: return value.boolean ? V_TRUE : V_FALSE;
    case T_SIGNED:
        if (value.signed_integer < INTPTR_MIN / 2 || value.signed_integer > INTPTR_MAX / 2)
            return failure(rt, "FFI range: result exceeds integer range");
        return value_int((intptr_t)value.signed_integer);
    case T_UNSIGNED:
        if (value.unsigned_integer > (uint64_t)(INTPTR_MAX / 2)) return failure(rt, "FFI range: result exceeds integer range");
        return value_int((intptr_t)value.unsigned_integer);
    case T_FLOAT: return rt_float(rt, value.single);
    case T_DOUBLE: return rt_float(rt, value.real);
    case T_CALLBACK:
        if (value.callback) return failure(rt, "FFI type: callbacks not implemented");
        return rt->constants[rt->foreign->null_constant];
    case T_POINTER: return wrap_pointer(rt, value.pointer);
    case T_UTF8: {
        if (!value.pointer) return rt->constants[rt->foreign->null_constant];
        size_t n = strlen(value.pointer);
        if (!utf8(value.pointer, n)) return failure(rt, "FFI encoding: invalid UTF-8 result");
        return rt_text(rt, K_STRING, value.pointer, n);
    }
    }
    return 0;
}

static Value allocate_memory(Runtime *rt, size_t size, const void *initial);
static bool access_memory(Runtime *rt, Pointer p, size_t offset, size_t bytes, size_t alignment);

static Value foreign_call(Runtime *rt, size_t callable, size_t first, size_t count, size_t *failed_argument, unsigned *expected_type) {
    Binding *binding = object_data(rt->stack[callable]);
    Signature *s = binding->signature;
    if (count != s->count) return failure(rt, "FFI arity: wrong number of arguments");
    ForeignSlot args[MAX_ARGUMENTS], result = {0};
    char *copies[MAX_ARGUMENTS];
    size_t lengths[MAX_ARGUMENTS];
    for (size_t i = 0; i < count; i++) copies[i] = NULL;
    bool ok = true;
    for (size_t i = 0; i < count; i++) {
        *failed_argument = i; *expected_type = s->arguments[i];
        Value value = rt->stack[first + i];
        if (s->arguments[i] == AGGREGATE) {
            Pointer p; ffi_type *type = s->argument_types[i];
            if (!pointer(rt, value, &p) || !access_memory(rt, p, 0, type->size, type->alignment)) { ok = false; break; }
            copies[i] = malloc(type->size); lengths[i] = type->size;
            if (!copies[i]) { ok = rt_error(rt, "out of host memory"); break; }
            memcpy(copies[i], (void *)p.address, type->size);
            continue;
        }
        const Scalar *type = &scalars[s->arguments[i]];
        if (type->kind == T_UTF8) {
            if (rt_kind(value, K_POINTER)) {
                Pointer p;
                if (!pointer(rt, value, &p)) { ok = false; break; }
                if (p.address) { ok = rt_error(rt, "FFI type: utf8 needs string or null pointer"); break; }
                args[i].pointer = NULL;
            } else {
                const char *str = c_string(rt, value);
                if (!str) { ok = false; break; }
                size_t n; text(value, &n);
                copies[i] = malloc(n + 1); lengths[i] = n + 1;
                if (!copies[i]) { ok = rt_error(rt, "out of host memory"); break; }
                memcpy(copies[i], str, n + 1); args[i].pointer = copies[i];
            }
        } else if (!to_foreign(rt, type, value, &args[i])) { ok = false; break; }
    }
    if (ok) { *failed_argument = SIZE_MAX; *expected_type = UINT_MAX; }
    Value value = 0, aggregate_result = V_NIL;
    Roots result_roots; roots_push(&rt->heap, &result_roots, &aggregate_result, 1);
    void *aggregate_bytes = NULL;
    if (ok && s->result == AGGREGATE) {
        size_t bytes = s->result_type->size;
        aggregate_bytes = calloc(1, bytes < sizeof(ffi_arg) ? sizeof(ffi_arg) : bytes);
        if (!aggregate_bytes) ok = rt_error(rt, "out of host memory");
        else { aggregate_result = allocate_memory(rt, bytes, NULL); ok = aggregate_result != 0; }
    }
    if (ok) {
        /* Leaf calls cannot reenter Otium. No managed allocation occurs while
         * C runs, and every string argument has independent native storage. */
        union { ForeignSlot slot; ffi_arg word; ffi_sarg signed_word; } storage[MAX_ARGUMENTS], returned = {0};
        void *addresses[MAX_ARGUMENTS];
        for (size_t i = 0; i < count; i++) {
            if (s->arguments[i] == AGGREGATE) addresses[i] = copies[i];
            else {
                memory_scalar(s->arguments[i], &storage[i], &args[i], true);
                addresses[i] = &storage[i];
            }
        }
        ffi_call(&s->cif, FFI_FN(binding->target), aggregate_bytes ? aggregate_bytes : &returned, addresses);
        if (s->result == AGGREGATE) {
            Pointer p; memcpy(&p, object_bytes(aggregate_result), sizeof(p));
            memcpy((void *)p.address, aggregate_bytes, s->result_type->size);
            value = aggregate_result;
        } else {
        const Scalar *type = &scalars[s->result];
        if (type->kind == T_SIGNED && type->size < sizeof(ffi_arg))
            result.signed_integer = returned.signed_word;
        else if (type->kind == T_UNSIGNED && type->size < sizeof(ffi_arg))
            result.unsigned_integer = returned.word;
        else if (type->kind == T_BOOL) result.boolean = returned.word != 0;
        else memory_scalar(s->result, &returned, &result, false);
        if (scalars[s->result].kind == T_POINTER && result.pointer)
            for (size_t i = 0; i < count; i++) {
                uintptr_t address = (uintptr_t)result.pointer, begin = (uintptr_t)copies[i];
                if (copies[i] && address >= begin && address - begin <= lengths[i]) {
                    ok = rt_error(rt, "FFI lifetime: pointer escapes temporary UTF-8 argument"); break;
                }
            }
        if (ok) { *expected_type = s->result; value = from_foreign(rt, &scalars[s->result], result); }
        }
    }
    roots_pop(&rt->heap, &result_roots);
    free(aggregate_bytes);
    for (size_t i = 0; i < count; i++) free(copies[i]);
    return value;
}

static bool nonnegative(Runtime *rt, Value value, size_t *out) {
    *out = 0;
    if (!value_is_int(value)) return rt_error(rt, "FFI type: expected exact integer");
    intptr_t n = value_to_int(value);
    if (n < 0) return rt_error(rt, "FFI range: expected nonnegative integer");
    *out = (size_t)n;
    return true;
}

static Value allocate_memory(Runtime *rt, size_t size, const void *initial) {
    ForeignState *state = rt->foreign;
    if (size > (size_t)(INTPTR_MAX / 2) - state->bytes) return failure(rt, "FFI range: allocation size overflow");
    Memory *m = calloc(1, sizeof(*m));
    if (!m) return failure(rt, "out of host memory");
    m->allocation = calloc(1, size ? size : 1);
    if (!m->allocation) { free(m); return failure(rt, "FFI allocation failed"); }
    if (initial && size) memcpy(m->allocation, initial, size);
    m->address = (uintptr_t)m->allocation; m->size = size;
    Value p = pointer_object(rt, (Pointer){m->address, m, size, true, false});
    if (!p) { free(m->allocation); free(m); return 0; }
    m->next = state->memory; state->memory = m; state->allocations++; state->bytes += size;
    return p;
}

static bool access_memory(Runtime *rt, Pointer p, size_t offset, size_t bytes, size_t alignment) {
    if (!p.address) return rt_error(rt, "FFI null pointer");
    if ((p.bounded && (offset > p.length || bytes > p.length - offset)) ||
        offset > UINTPTR_MAX - p.address || bytes > UINTPTR_MAX - (p.address + offset))
        return rt_error(rt, "FFI bounds: memory access out of bounds");
    if ((p.address + offset) % alignment) return rt_error(rt, "FFI alignment: unaligned access");
    return true;
}

/* Use the declared C type's representation, not a prefix of ForeignSlot.
 * This also avoids assumptions about byte order for narrow integers. */
#define MEMORY_CASE(i,t,member) case i: { t v; if (write) { v=(t)value->member; memcpy(address,&v,sizeof(v)); } \
    else { memcpy(&v,address,sizeof(v)); value->member=v; } break; }
static void memory_scalar(unsigned type, void *address, ForeignSlot *value, bool write) {
    switch (type) {
    case 1:
        if (write) memcpy(address, &value->boolean, sizeof(bool));
        else value->boolean = *(unsigned char *)address != 0;
        break;
    MEMORY_CASE(2,int8_t,signed_integer) MEMORY_CASE(3,uint8_t,unsigned_integer)
    MEMORY_CASE(4,int16_t,signed_integer) MEMORY_CASE(5,uint16_t,unsigned_integer)
    MEMORY_CASE(6,int32_t,signed_integer) MEMORY_CASE(7,uint32_t,unsigned_integer)
    MEMORY_CASE(8,int64_t,signed_integer) MEMORY_CASE(9,uint64_t,unsigned_integer)
    MEMORY_CASE(10,short,signed_integer) MEMORY_CASE(11,unsigned short,unsigned_integer)
    MEMORY_CASE(12,int,signed_integer) MEMORY_CASE(13,unsigned int,unsigned_integer)
    MEMORY_CASE(14,long,signed_integer) MEMORY_CASE(15,unsigned long,unsigned_integer)
    MEMORY_CASE(16,size_t,unsigned_integer) MEMORY_CASE(17,ptrdiff_t,signed_integer)
    MEMORY_CASE(18,float,single) MEMORY_CASE(19,double,real)
    MEMORY_CASE(20,void *,pointer) MEMORY_CASE(21,ForeignCallback,callback)
    MEMORY_CASE(22,void *,pointer)
    }
}
#undef MEMORY_CASE

/* Descriptions are private snapshots; callers only receive detached trees. */
static Value layout_copy(Runtime *rt, Value value, unsigned depth, size_t *budget) {
    if (!*budget || depth > 256) return failure(rt, "FFI type: layout description too deep or cyclic");
    --*budget;
    if (value == V_EMPTY || value_is_int(value) || rt_kind(value, K_SYMBOL)) return value;
    if (!rt_kind(value, K_PAIR)) return failure(rt, "FFI type: invalid layout description");
    Value live[] = {value, V_NIL, V_NIL}; Roots roots;
    roots_push(&rt->heap, &roots, live, 3);
    live[1] = layout_copy(rt, object_fields(live[0])[0], depth + 1, budget);
    if (live[1]) live[2] = layout_copy(rt, object_fields(live[0])[1], depth + 1, budget);
    Value result = 0;
    if (live[1] && live[2]) {
        result = rt_alloc(rt, K_PAIR, 2, 0);
        if (result) { object_fields(result)[0] = live[1]; object_fields(result)[1] = live[2]; }
    }
    roots_pop(&rt->heap, &roots); return result;
}

static Value shared_builtin(Runtime *rt,unsigned id,size_t first,size_t count) {
    if (id == B_FFI_LAYOUT || id == B_FFI_LAYOUT_DATA) {
        if (count != 1) return failure(rt, "FFI arity: layout operation needs one argument");
        Value input = rt->stack[first];
        if (id == B_FFI_LAYOUT_DATA) {
            if (!rt_kind(input, K_FOREIGN_LAYOUT)) return V_FALSE;
            input = object_fields(input)[0];
        }
        size_t budget = 65536;
        Value description = layout_copy(rt, input, 0, &budget);
        if (!description || id == B_FFI_LAYOUT_DATA) return description;
        Roots roots; roots_push(&rt->heap, &roots, &description, 1);
        Value result = rt_alloc(rt, K_FOREIGN_LAYOUT, 1, 0);
        if (result) object_fields(result)[0] = description;
        roots_pop(&rt->heap, &roots); return result;
    }

    if(id==B_FFI_FAIL) {
        if(count!=3) return failure(rt,"FFI arity: fail requires operation, reason and message");
        const char *op=c_string(rt,rt->stack[first]),*reason=c_string(rt,rt->stack[first+1]),*message=c_string(rt,rt->stack[first+2]);
        return op && reason && message ? rt_ffi_error(rt,op,reason,message) : 0;
    }
    if(id==B_FFI_ERROR_FIELD) {
        if(count!=2) return failure(rt,"FFI arity: error-field needs two arguments");
        const char *field=c_string(rt,rt->stack[first+1]); if(!field) return 0;
        if(!rt_kind(rt->stack[first],K_TABLE)) return V_FALSE;
        size_t n=strlen(field); char *name=malloc(n+2);
        if(!name) return failure(rt,"out of host memory");
        name[0]=':'; memcpy(name+1,field,n+1);
        Value key=rt_intern(rt,K_KEYWORD,name,n+1); free(name);
        return key ? rt_table_get(rt,rt->stack[first],key,V_FALSE) : 0;
    }
    size_t wanted=id==B_FFI_RAW_NULL || id==B_FFI_LOADER_ERROR ? 0 : id==B_FFI_RAW_BIND ? 5 :
        id==B_FFI_RAW_ALLOC ? 2 : id==B_FFI_RAW_REF ? 3 : id==B_FFI_RAW_SET ? 4 : 1;
    if(count!=wanted) return failure(rt,"FFI arity: wrong number of arguments");
    if(id==B_FFI_RAW_NULL) return rt->constants[rt->foreign->null_constant];
    if(id==B_FFI_LOADER_ERROR) return rt_text(rt,K_STRING,rt->foreign->loader_error,strlen(rt->foreign->loader_error));
    if(id==B_FFI_RAW_BIND) return bind_function(rt,first,true);
    if(id==B_FFI_RAW_OPEN) {
        const char *name=c_string(rt,rt->stack[first]); if(!name) return 0;
        void *handle=dlopen(name,RTLD_NOW|RTLD_LOCAL);
        if(!handle) { const char *error=dlerror(); snprintf(rt->foreign->loader_error,sizeof(rt->foreign->loader_error),"%s",error ? error : "load failed"); return V_FALSE; }
        for(Library *p=rt->foreign->libraries;p;p=p->next) if(p->handle==handle) { dlclose(handle); return library_object(rt,p,rt->stack[first]); }
        Library *library=calloc(1,sizeof(*library));
        if(!library) { dlclose(handle); return failure(rt,"out of host memory"); }
        library->handle=handle; Value result=library_object(rt,library,rt->stack[first]);
        if(!result) { dlclose(handle); free(library); return 0; }
        library->next=rt->foreign->libraries; rt->foreign->libraries=library; return result;
    }
    if(id==B_FFI_RAW_ALLOC) {
        size_t size,alignment;
        if(!nonnegative(rt,rt->stack[first],&size) || !nonnegative(rt,rt->stack[first+1],&alignment)) return 0;
        if(!alignment || (alignment&(alignment-1)) || alignment>_Alignof(max_align_t)) return failure(rt,"FFI type: invalid allocation alignment");
        return allocate_memory(rt,size,NULL);
    }
    if(id==B_FFI_RAW_SIZE || id==B_FFI_RAW_ALIGN) {
        int type=scalar_code(rt,rt->stack[first],false); if(type<0) return 0;
        return value_int((intptr_t)(id==B_FFI_RAW_SIZE ? scalars[type].size : scalars[type].alignment));
    }
    Pointer p; size_t offset; int type=scalar_code(rt,rt->stack[first+1],true);
    if(type<0 || !pointer(rt,rt->stack[first],&p) || !nonnegative(rt,rt->stack[first+2],&offset)) return 0;
    const Scalar *s=&scalars[type];
    if(!access_memory(rt,p,offset,s->size,s->alignment)) return 0;
    ForeignSlot slot={0};
    if(id==B_FFI_RAW_SET && !to_foreign(rt,s,rt->stack[first+3],&slot)) return 0;
    memory_scalar((unsigned)type,(void *)(p.address+offset),&slot,id==B_FFI_RAW_SET);
    return id==B_FFI_RAW_SET ? V_FALSE : from_foreign(rt,s,slot);
}

static Value bulk_memory(Runtime *rt, unsigned id, size_t first, size_t count) {
    size_t min = id == B_FFI_COPY ? 3 : 2;
    size_t max = id == B_FFI_WRITE_BYTES ? 4 : min;
    if (count < min || count > max) return failure(rt, "FFI arity: wrong number of arguments");
    Value *args = rt->stack + first;
    Pointer dest;
    if (!pointer(rt, args[0], &dest)) return 0;
    if (id == B_FFI_COPY) {
        Pointer src; size_t bytes;
        if (!pointer(rt, args[1], &src) || !nonnegative(rt, args[2], &bytes) ||
            !access_memory(rt, dest, 0, bytes, 1) || !access_memory(rt, src, 0, bytes, 1)) return 0;
        memmove((void *)dest.address, (const void *)src.address, bytes);
        return V_FALSE;
    }
    if (id == B_FFI_WRITE_BYTES) {
        if (!rt_kind(args[1], K_BYTEVECTOR)) return failure(rt, "FFI type: expected bytevector");
        size_t length = rt_bytes_length(args[1]), start = 0, end = length;
        if ((count > 2 && !nonnegative(rt, args[2], &start)) ||
            (count > 3 && !nonnegative(rt, args[3], &end))) return 0;
        if (start > end || end > length) return failure(rt, "FFI range: invalid bytevector slice");
        if (!access_memory(rt, dest, 0, end - start, 1)) return 0;
        memcpy((void *)dest.address, rt_bytes_data(args[1]) + start, end - start);
        return V_FALSE;
    }
    size_t bytes;
    if (!nonnegative(rt, args[1], &bytes) || !access_memory(rt, dest, 0, bytes, 1)) return 0;
    if (id == B_FFI_READ_UTF8_BYTES) {
        if (!utf8((const unsigned char *)dest.address, bytes)) return failure(rt, "FFI encoding: invalid UTF-8");
        return rt_text(rt, K_STRING, (const char *)dest.address, bytes);
    }
    Value result = rt_bytes_new(rt, bytes);
    if (result) memcpy(rt_bytes_data(result), (const void *)dest.address, bytes);
    return result;
}

static Value foreign_builtin(Runtime *rt, unsigned id, size_t first, size_t count) {
    if (id >= B_FFI_COPY && id <= B_FFI_READ_UTF8_BYTES) return bulk_memory(rt, id, first, count);
    if(id>=B_FFI_RAW_NULL) return shared_builtin(rt,id,first,count);
    size_t arity = id == B_FFI_BIND ? 5 : id == B_FFI_SET ? 4 :
        id == B_FFI_REF || id == B_FFI_VIEW ? 3 :
        id == B_FFI_HAS_SYMBOL || id == B_FFI_ALLOC || id == B_FFI_POINTER_EQ || id == B_FFI_READ_UTF8 ? 2 :
        id == B_FFI_COUNT || id == B_FFI_BYTES ? 0 : 1;
    if (count != arity) return failure(rt, "FFI arity: wrong number of arguments");
    Value *args = rt->stack + first;
    if (id == B_FFI_OPEN) return open_library(rt, args[0]);
    if (id == B_FFI_BIND) return bind_function(rt, first, false);
    if (id == B_FFI_COUNT) return value_int((intptr_t)rt->foreign->allocations);
    if (id == B_FFI_BYTES) return value_int((intptr_t)rt->foreign->bytes);
    if (id == B_FFI_POINTER) return rt_kind(args[0], K_POINTER) ? V_TRUE : V_FALSE;
    if (id == B_FFI_NULL) {
        if (!rt_kind(args[0], K_POINTER)) return V_FALSE;
        Pointer p; memcpy(&p, object_bytes(args[0]), sizeof(p));
        return !p.address ? V_TRUE : V_FALSE;
    }
    if (id == B_FFI_HAS_SYMBOL) {
        if (!rt_kind(args[0], K_FOREIGN_LIBRARY)) return failure(rt, "FFI type: expected library");
        const char *name = c_string(rt, args[1]);
        if (!name) return 0;
        Library *lib = object_data(args[0]);
        dlerror(); void *target = dlsym(lib->handle, name);
        return !dlerror() && target ? V_TRUE : V_FALSE;
    }
    if (id == B_FFI_ALLOC_UTF8) {
        const char *s = c_string(rt, args[0]);
        if (!s) return 0;
        size_t n; text(args[0], &n);
        return allocate_memory(rt, n + 1, s);
    }
    if (id == B_FFI_SIZEOF || id == B_FFI_ALIGNOF || id == B_FFI_ALLOC) {
        int type = scalar(rt, args[0], true);
        if (type < 0) return 0;
        if (id != B_FFI_ALLOC) return value_int((intptr_t)(id == B_FFI_SIZEOF ? scalars[type].size : scalars[type].alignment));
        size_t n;
        if (!nonnegative(rt, args[1], &n)) return 0;
        if (n > SIZE_MAX / scalars[type].size) return failure(rt, "FFI range: allocation size overflow");
        return allocate_memory(rt, n * scalars[type].size, NULL);
    }
    if (id == B_FFI_FREE) {
        if (!rt_kind(args[0], K_POINTER)) return failure(rt, "FFI type: expected pointer");
        Pointer p; memcpy(&p, object_bytes(args[0]), sizeof(p));
        if (!p.address) return V_FALSE;
        if (!p.owner || p.view) return failure(rt, "FFI type: free needs owning pointer");
        if (!p.owner->released) {
            free(p.owner->allocation); p.owner->allocation = NULL; p.owner->released = true;
            rt->foreign->allocations--; rt->foreign->bytes -= p.owner->size;
        }
        return V_FALSE;
    }
    Pointer p;
    if (!pointer(rt, args[0], &p)) return 0;
    if (id == B_FFI_POINTER_EQ) {
        Pointer other;
        if (!pointer(rt, args[1], &other)) return 0;
        return p.address == other.address ? V_TRUE : V_FALSE;
    }
    if (id == B_FFI_VIEW) {
        size_t offset, bytes;
        if (!nonnegative(rt, args[1], &offset) || !nonnegative(rt, args[2], &bytes)) return 0;
        if (!access_memory(rt, p, offset, bytes, 1)) return 0;
        p.address += offset; p.length = bytes; p.bounded = true; p.view = true;
        return pointer_object(rt, p);
    }
    if (id == B_FFI_READ_UTF8) {
        size_t limit;
        if (!nonnegative(rt, args[1], &limit) || !access_memory(rt, p, 0, limit, 1)) return 0;
        const char *s = (const char *)p.address;
        const char *end = memchr(s, 0, limit);
        if (!end) return failure(rt, "FFI bounds: missing string terminator");
        if (!utf8((const unsigned char *)s, (size_t)(end - s))) return failure(rt, "FFI encoding: invalid UTF-8");
        return rt_text(rt, K_STRING, s, (size_t)(end - s));
    }
    if (id == B_FFI_REF || id == B_FFI_SET) {
        int type = scalar(rt, args[1], true);
        size_t index;
        if (type < 0 || !nonnegative(rt, args[2], &index)) return 0;
        const Scalar *s = &scalars[type];
        if (index > SIZE_MAX / s->size) return failure(rt, "FFI range: offset overflow");
        size_t offset = index * s->size;
        if (!access_memory(rt, p, offset, s->size, s->alignment)) return 0;
        ForeignSlot slot = {0};
        if (id == B_FFI_SET && !to_foreign(rt, s, args[3], &slot)) return 0;
        memory_scalar((unsigned)type, (void *)(p.address + offset), &slot, id == B_FFI_SET);
        return id == B_FFI_SET ? V_FALSE : from_foreign(rt, s, slot);
    }
    return failure(rt, "unknown FFI operation");
}

static Value foreign_result(Runtime *rt,Value result,const char *op) {
    if(result || rt->raised || rt->exit_pending || rt->interrupted || rt->restart_pending || strncmp(rt->error,"FFI ",4)) return result;
    char reason[64]; size_t n=strcspn(rt->error+4,": ");
    if(n>=sizeof(reason)) n=sizeof(reason)-1;
    memcpy(reason,rt->error+4,n); reason[n]=0;
    return rt_ffi_error(rt,op,reason,rt->error);
}
static bool new_ffi_error(Runtime *rt) {
    return !rt->raised && !rt->exit_pending && !rt->interrupted && !rt->restart_pending &&
        !strncmp(rt->error, "FFI ", 4);
}
static bool condition_field(Runtime *rt, const char *name, Value value) {
    Roots roots; roots_push(&rt->heap, &roots, &value, 1);
    Value key = rt_intern(rt, K_KEYWORD, name, strlen(name));
    bool ok = key && rt_table_set(rt, rt->condition_values[0], key, value);
    roots_pop(&rt->heap, &roots); return ok;
}
static bool library_context(Runtime *rt, Value library, Value symbol) {
    Value live[] = {library, symbol}; Roots roots;
    roots_push(&rt->heap, &roots, live, 2);
    bool ok = true;
    if (rt_kind(live[0], K_FOREIGN_LIBRARY))
        ok = condition_field(rt, ":library", object_fields(live[0])[0]);
    if (ok && rt_kind(live[1], K_STRING)) ok = condition_field(rt, ":symbol", live[1]);
    roots_pop(&rt->heap, &roots); return ok;
}
Value rt_foreign_call(Runtime *rt,size_t callable,size_t first,size_t count) {
    size_t argument = SIZE_MAX; unsigned expected = UINT_MAX;
    Value result = foreign_call(rt,callable,first,count,&argument,&expected);
    bool context = !result && new_ffi_error(rt);
    result = foreign_result(rt,result,"call");
    if (!context || !rt->raised) return result;
    Value function = rt->stack[callable];
    if (!library_context(rt, object_fields(function)[0], object_fields(function)[1])) return 0;
    if (argument != SIZE_MAX && !condition_field(rt, ":argument", value_int((intptr_t)argument))) return 0;
    Value type = V_FALSE;
    if (expected < AGGREGATE) type = rt_intern(rt, K_SYMBOL, scalars[expected].name, strlen(scalars[expected].name));
    else if (expected == AGGREGATE) {
        type = rt_alloc(rt, K_FOREIGN_LAYOUT, 1, 0);
        if (type) {
            function = rt->stack[callable];
            object_fields(type)[0] = argument == SIZE_MAX ? object_fields(function)[3] :
                list_item(object_fields(function)[2], argument);
        }
    }
    if (type) condition_field(rt, ":expected-type", type);
    return 0;
}
Value rt_foreign_builtin(Runtime *rt,unsigned id,size_t first,size_t count) {
    const char *op=id==B_FFI_BIND || id==B_FFI_RAW_BIND ? "bind" :
        id==B_FFI_OPEN || id==B_FFI_RAW_OPEN ? "open-library" :
        id==B_FFI_REF || id==B_FFI_RAW_REF ? "ref" : id==B_FFI_SET || id==B_FFI_RAW_SET ? "set!" :
        id==B_FFI_COPY ? "copy!" : id==B_FFI_READ_BYTES ? "read-bytes" :
        id==B_FFI_WRITE_BYTES ? "write-bytes!" : id==B_FFI_READ_UTF8_BYTES ? "read-utf8-bytes" : "foreign";
    Value result = foreign_builtin(rt,id,first,count);
    bool context = !result && new_ffi_error(rt) && (id == B_FFI_BIND || id == B_FFI_RAW_BIND) && count >= 2;
    result = foreign_result(rt,result,op);
    if (context && rt->raised) library_context(rt, rt->stack[first], rt->stack[first + 1]);
    return result;
}
