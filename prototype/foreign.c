#include "internal.h"
#include "foreign.h"
#include <dlfcn.h>
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
typedef struct Signature Signature;
struct Signature {
    Signature *next;
    size_t count;
    unsigned arguments[MAX_ARGUMENTS], result;
    void *module;
    ForeignAdapter adapter;
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
    double compile_seconds;
};

size_t runtime_foreign_signatures(const Runtime *rt) {
    size_t count = 0;
    for (Signature *s = rt->foreign->signatures; s; s = s->next) count++;
    return count;
}

double runtime_foreign_compile_seconds(const Runtime *rt) { return rt->foreign->compile_seconds; }

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

static Value object(Runtime *rt, unsigned kind, void *data) {
    Value v = rt_alloc(rt, kind, 0, sizeof(data));
    if (v) memcpy(object_bytes(v), &data, sizeof(data));
    return v;
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
    /* Keep four recently bound, unused adapters to avoid recompiling common
     * signatures during reload. Live bindings and in-progress wrappers pin theirs. */
    size_t spare = 0;
    Signature **signature = &state->signatures;
    while (*signature) {
        Signature *p = *signature;
        if (p->marked || p->pending || spare++ < 4) signature = &p->next;
        else {
            *signature = p->next;
            dlclose(p->module);
            free(p);
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
        if (state->signatures->module) dlclose(state->signatures->module);
        free(state->signatures); state->signatures = next;
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
            return object(rt, K_FOREIGN_LIBRARY, existing);
        }
        Library *library = calloc(1, sizeof(*library));
        if (!library) { dlclose(handle); return failure(rt, "out of host memory"); }
        library->handle = handle;
        Value v = object(rt, K_FOREIGN_LIBRARY, library);
        if (!v) { dlclose(handle); free(library); return 0; }
        library->next = rt->foreign->libraries; rt->foreign->libraries = library;
        return v;
    }
    return failure(rt, "FFI load: could not open a candidate library");
}

static bool emit_adapter(Runtime *rt, FILE *out, void *context) {
    (void)rt;
    Signature *s = context;
    const Scalar *result = &scalars[s->result];
    /* Pointer declarations describe ABI classes without C pointee types.
     * Clang's exact function-type sanitizer rejects valid declarations such
     * as void* for an int* parameter, so disable that check at this boundary. */
    fputs("#include \"foreign.h\"\n#include <string.h>\n"
          "#if defined(__clang__)\n__attribute__((no_sanitize(\"function\")))\n#endif\n"
          "void otium_foreign_adapter(void *target,const ForeignSlot *args,ForeignSlot *result) {\n", out);
    fprintf(out, "typedef %s (*Function)(", result->c_type);
    if (!s->count) fputs("void", out);
    for (size_t i = 0; i < s->count; i++) fprintf(out, "%s%s", i ? "," : "", scalars[s->arguments[i]].c_type);
    fputs("); Function fn; _Static_assert(sizeof(fn)==sizeof(target),\"function pointer size\");"
          " memcpy(&fn,&target,sizeof(fn));\n", out);
    if (result->kind != T_VOID) fprintf(out, "result->%s=", result->member);
    if (result->kind == T_UTF8) fputs("(void *)", out);
    fputs("fn(", out);
    for (size_t i = 0; i < s->count; i++) {
        const Scalar *a = &scalars[s->arguments[i]];
        fprintf(out, "%s(%s)args[%zu].%s", i ? "," : "", a->c_type, i, a->member);
    }
    fputs(");\n}\n", out);
    return !ferror(out);
}

static Value bind_function(Runtime *rt, size_t first) {
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
    int type = scalar(rt, args[3], false);
    if (type < 0) return 0;
    wanted.result = (unsigned)type;
    Value list = args[2];
    for (size_t i = 0; i < wanted.count; i++, list = object_fields(list)[1]) {
        type = scalar(rt, object_fields(list)[0], false);
        if (type < 0) return 0;
        if (scalars[type].kind == T_VOID) return failure(rt, "FFI signature: void argument");
        wanted.arguments[i] = (unsigned)type;
    }
    Signature *s;
    for (s = rt->foreign->signatures; s; s = s->next)
        if (s->count == wanted.count && s->result == wanted.result &&
            !memcmp(s->arguments, wanted.arguments, s->count * sizeof(unsigned))) break;
    if (s && s != rt->foreign->signatures) {
        Signature **link = &rt->foreign->signatures;
        while (*link != s) link = &(*link)->next;
        *link = s->next;
        s->next = rt->foreign->signatures;
        rt->foreign->signatures = s;
    }
    if (!s) {
        s = malloc(sizeof(*s));
        if (!s) return failure(rt, "out of host memory");
        *s = wanted;
        void *entry;
        if (!rt_module_compile(rt, emit_adapter, s, "otium_foreign_adapter", &s->module,
                &entry, &rt->foreign->compile_seconds)) { free(s); return 0; }
        _Static_assert(sizeof(entry) == sizeof(s->adapter), "adapter pointer size");
        memcpy(&s->adapter, &entry, sizeof(entry));
        s->next = rt->foreign->signatures; rt->foreign->signatures = s;
    }
    Binding *b = malloc(sizeof(*b));
    if (!b) return failure(rt, "out of host memory");
    *b = (Binding){rt->foreign->bindings, s, target, args[4] == V_TRUE, false};
    s->pending = true;
    Value value = object(rt, K_FOREIGN_FUNCTION, b);
    s->pending = false;
    if (!value) { free(b); return 0; }
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

Value rt_foreign_call(Runtime *rt, size_t callable, size_t first, size_t count) {
    Binding *binding = object_data(rt->stack[callable]);
    Signature *s = binding->signature;
    if (count != s->count) return failure(rt, "FFI arity: wrong number of arguments");
    ForeignSlot args[MAX_ARGUMENTS], result = {0};
    char *copies[MAX_ARGUMENTS];
    size_t lengths[MAX_ARGUMENTS];
    for (size_t i = 0; i < count; i++) copies[i] = NULL;
    bool ok = true;
    for (size_t i = 0; i < count; i++) {
        const Scalar *type = &scalars[s->arguments[i]];
        Value value = rt->stack[first + i];
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
    Value value = 0;
    if (ok) {
        /* Leaf calls cannot reenter Otium. No managed allocation occurs while
         * C runs, and every string argument has independent native storage. */
        binding->signature->adapter(binding->target, args, &result);
        if (scalars[s->result].kind == T_POINTER && result.pointer)
            for (size_t i = 0; i < count; i++) {
                uintptr_t address = (uintptr_t)result.pointer, begin = (uintptr_t)copies[i];
                if (copies[i] && address >= begin && address - begin <= lengths[i]) {
                    ok = rt_error(rt, "FFI lifetime: pointer escapes temporary UTF-8 argument"); break;
                }
            }
        if (ok) value = from_foreign(rt, &scalars[s->result], result);
    }
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
    if (!p.bounded || offset > p.length || bytes > p.length - offset) return rt_error(rt, "FFI bounds: memory access out of bounds");
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
    }
}
#undef MEMORY_CASE

Value rt_foreign_builtin(Runtime *rt, unsigned id, size_t first, size_t count) {
    size_t arity = id == B_FFI_BIND ? 5 : id == B_FFI_SET ? 4 :
        id == B_FFI_REF || id == B_FFI_VIEW ? 3 :
        id == B_FFI_HAS_SYMBOL || id == B_FFI_ALLOC || id == B_FFI_POINTER_EQ || id == B_FFI_READ_UTF8 ? 2 :
        id == B_FFI_COUNT || id == B_FFI_BYTES ? 0 : 1;
    if (count != arity) return failure(rt, "FFI arity: wrong number of arguments");
    Value *args = rt->stack + first;
    if (id == B_FFI_OPEN) return open_library(rt, args[0]);
    if (id == B_FFI_BIND) return bind_function(rt, first);
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
        p.address += offset; p.length = bytes; p.view = true;
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
