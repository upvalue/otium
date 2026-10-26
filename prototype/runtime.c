#include "internal.h"
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

bool rt_error(Runtime *rt, const char *message) {
    if (!rt->error[0]) snprintf(rt->error, sizeof(rt->error), "%s", message);
    return false;
}

bool rt_grow(Runtime *rt, void **data, size_t *capacity, size_t needed, size_t width) {
    if (needed <= *capacity) return true;
    size_t next = *capacity ? *capacity : 16;
    while (next < needed) {
        if (next > SIZE_MAX / 2) return rt_error(rt, "allocation size overflow");
        next *= 2;
    }
    if (next > SIZE_MAX / width) return rt_error(rt, "allocation size overflow");
    void *new_data = realloc(*data, next * width);
    if (!new_data) return rt_error(rt, "out of host memory");
    *data = new_data;
    *capacity = next;
    return true;
}

bool rt_stack(Runtime *rt, size_t needed) {
    if (needed > rt->stack_limit) return rt_error(rt, "VM stack limit exceeded");
    if (!rt_grow(rt, (void **)&rt->stack, &rt->stack_capacity, needed, sizeof(Value))) return false;
    rt->stack_roots.slots = rt->stack;
    return true;
}

Value rt_alloc(Runtime *rt, uint32_t kind, uint32_t fields, size_t bytes) {
    size_t collections = rt->heap.collections;
    if (rt->stress_gc) heap_collect(&rt->heap);
    Value v = heap_alloc(&rt->heap, kind, fields, bytes);
    if (rt->heap.collections != collections) rt_foreign_collect(rt);
    if (!v) rt_error(rt, "managed heap exhausted");
    return v;
}

uint32_t rt_constant(Runtime *rt, Value value) {
    for (size_t i = 0; i < rt->constant_count; i++)
        if (rt->constant_state[i] && rt->constants[i] == value) return (uint32_t)i;
    size_t index = rt->constant_free;
    while (index < rt->constant_count && rt->constant_state[index]) index++;
    if (index >= UINT32_MAX || !rt_grow(rt, (void **)&rt->constants,
            &rt->constant_capacity, index + 1, sizeof(Value))) return UINT32_MAX;
    rt->constant_roots.slots = rt->constants;
    if (!rt_grow(rt, (void **)&rt->constant_state, &rt->constant_state_capacity, index + 1, 1)) return UINT32_MAX;
    if (index == rt->constant_count) rt->constant_count++;
    rt->constant_state[index] = rt->compiling ? 2 : 1;
    rt->constants[index] = value;
    rt->constant_free = index + 1;
    rt->constant_roots.count = rt->constant_count;
    return (uint32_t)index;
}

uint32_t rt_namespace(Runtime *rt, const char *name, size_t length) {
    for (size_t i = 0; i < rt->namespace_count; i++)
        if (strlen(rt->namespaces[i]) == length && !memcmp(rt->namespaces[i], name, length)) return (uint32_t)i;
    if (rt->namespace_count >= UINT32_MAX || !rt_grow(rt, (void **)&rt->namespaces,
            &rt->namespace_capacity, rt->namespace_count + 1, sizeof(char *))) return UINT32_MAX;
    char *copy = malloc(length + 1);
    if (!copy) { rt_error(rt, "out of host memory"); return UINT32_MAX; }
    memcpy(copy, name, length); copy[length] = 0;
    uint32_t id = (uint32_t)rt->namespace_count++;
    rt->namespaces[id] = copy;
    return id;
}

static uint32_t find_global(Runtime *rt, uint32_t space, const char *name, size_t length) {
    for (size_t i = 0; i < rt->global_count; i++)
        if (rt->global_namespaces[i] == space && strlen(rt->names[i]) == length &&
            !memcmp(rt->names[i], name, length)) return (uint32_t)i;
    return UINT32_MAX;
}

static uint32_t namespace_global(Runtime *rt, uint32_t space, const char *name, size_t length) {
    uint32_t existing = find_global(rt, space, name, length);
    if (existing != UINT32_MAX) return existing;
    if (rt->global_count >= UINT32_MAX ||
        !rt_grow(rt, (void **)&rt->globals, &rt->global_capacity, rt->global_count + 1, sizeof(Value))) {
        rt_error(rt, "too many globals"); return UINT32_MAX;
    }
    rt->global_roots.slots = rt->globals;
    if (!rt_grow(rt, (void **)&rt->names, &rt->name_capacity, rt->global_count + 1, sizeof(char *)))
        return UINT32_MAX;
    if (!rt_grow(rt, (void **)&rt->global_defined, &rt->global_defined_capacity,
                 rt->global_count + 1, 1)) return UINT32_MAX;
    if (!rt_grow(rt, (void **)&rt->global_namespaces, &rt->global_namespace_capacity,
                 rt->global_count + 1, sizeof(uint32_t))) return UINT32_MAX;
    if (!rt_grow(rt, (void **)&rt->global_private, &rt->global_private_capacity,
                 rt->global_count + 1, 1)) return UINT32_MAX;
    char *copy = malloc(length + 1);
    if (!copy) { rt_error(rt, "out of host memory"); return UINT32_MAX; }
    memcpy(copy, name, length); copy[length] = 0;
    uint32_t index = (uint32_t)rt->global_count++;
    rt->names[index] = copy;
    rt->globals[index] = V_UNBOUND;
    rt->global_defined[index] = 0;
    rt->global_private[index] = 0;
    rt->global_namespaces[index] = space;
    rt->global_roots.count = rt->global_count;
    return index;
}

uint32_t rt_global(Runtime *rt, const char *name, size_t length) {
    if (rt_import_global(rt, name, length) != UINT32_MAX) {
        rt_error(rt, "definition conflicts with a referred import"); return UINT32_MAX;
    }
    return namespace_global(rt, rt->current_namespace, name, length);
}

uint32_t rt_binding_global(Runtime *rt, uint32_t space, const char *name, size_t length, bool create) {
    uint32_t index = find_global(rt, space, name, length);
    if (index != UINT32_MAX) return index;
    index = rt_import_global_in(rt, space, name, length);
    if (index != UINT32_MAX) return index;
    index = find_global(rt, 0, name, length);
    if (index != UINT32_MAX) return index;
    return create ? namespace_global(rt, space, name, length) : UINT32_MAX;
}

static uint32_t resolve_global(Runtime *rt, const char *name, size_t length, bool create) {
    uint32_t local = find_global(rt, rt->current_namespace, name, length);
    if (local != UINT32_MAX) return local;
    uint32_t imported = rt_import_global(rt, name, length);
    if (imported != UINT32_MAX) return imported;
    /* A local name containing a slash takes precedence over qualification. */
    const char *slash = memchr(name, '/', length);
    if (slash && slash != name && slash + 1 < name + length) {
        size_t prefix = (size_t)(slash - name);
        uint32_t alias = rt_alias_namespace(rt, name, prefix);
        if (alias != UINT32_MAX)
            return rt_binding_global(rt, alias, slash + 1, length - prefix - 1, create);
        for (size_t i = 0; i < rt->namespace_count; i++)
            if (strlen(rt->namespaces[i]) == prefix && !memcmp(rt->namespaces[i], name, prefix))
                return rt_binding_global(rt, (uint32_t)i, slash + 1, length - prefix - 1, create);
    }
    uint32_t core = find_global(rt, 0, name, length);
    if (core != UINT32_MAX) return core;
    return create ? rt_global(rt, name, length) : UINT32_MAX;
}
uint32_t rt_resolve_global(Runtime *rt, const char *name, size_t length) {
    return resolve_global(rt, name, length, true);
}

Value rt_text(Runtime *rt, uint32_t kind, const char *text, size_t length) {
    if (length > SIZE_MAX - sizeof(size_t) - 1) { rt_error(rt, "text too large"); return 0; }
    Value v = rt_alloc(rt, kind, 0, sizeof(size_t) + length + 1);
    if (v) {
        unsigned char *bytes = object_bytes(v);
        memcpy(bytes, &length, sizeof(length));
        memcpy(bytes + sizeof(length), text, length);
    }
    return v;
}

Value rt_float(Runtime *rt, double number) {
    Value v = rt_alloc(rt, K_FLOAT, 0, sizeof(double));
    if (v) memcpy(object_bytes(v), &number, sizeof(number));
    return v;
}

Runtime *runtime_create(size_t bytes, size_t stack_limit) {
    Runtime *rt = calloc(1, sizeof(*rt));
    if (!rt) return NULL;
    if (!heap_init(&rt->heap, bytes)) { free(rt); return NULL; }
    rt->stack_limit = stack_limit;
    rt->required_namespace = UINT32_MAX;
    roots_push(&rt->heap, &rt->constant_roots, NULL, 0);
    roots_push(&rt->heap, &rt->global_roots, NULL, 0);
    roots_push(&rt->heap, &rt->stack_roots, NULL, 0);
    if (rt_namespace(rt, "otium.core", 10) == UINT32_MAX ||
        !rt_builtins(rt) || !rt_foreign_init(rt)) { runtime_destroy(rt); return NULL; }
    rt->current_namespace = rt_namespace(rt, "user", 4);
    if (rt->current_namespace == UINT32_MAX) { runtime_destroy(rt); return NULL; }
    rt->permanent_constants = rt->constant_count;
    return rt;
}

void runtime_destroy(Runtime *rt) {
    if (!rt) return;
    rt_foreign_destroy(rt);
    rt_modules_destroy(rt);
    for (size_t i = 0; i < rt->function_count; i++) {
        Function *f = rt->functions[i];
        rt_free_function(f);
    }
    for (size_t i = 0; i < rt->global_count; i++) free(rt->names[i]);
    for (size_t i = 0; i < rt->namespace_count; i++) free(rt->namespaces[i]);
    free(rt->namespaces); free(rt->global_namespaces);
    free(rt->functions); free(rt->names); free(rt->constants); free(rt->globals);
    free(rt->frames); free(rt->stack); free(rt->constant_state); free(rt->global_defined); free(rt->global_private);
    heap_destroy(&rt->heap);
    free(rt);
}

/* Top-level begin forms are evaluated in order, so declarations from earlier
 * forms are visible before later forms are compiled. */
static bool evaluate_top(Runtime *rt, const Ast *form, Value *result) {
    if (form->kind == AST_LIST && !form->as.list.tail &&
        ast_symbol(form->as.list.first, "begin") && form->as.list.first->next) {
        for (const Ast *child = form->as.list.first->next; child; child = child->next)
            if (!evaluate_top(rt, child, result)) return false;
        return true;
    }
    bool ok;
    uint32_t function;
    rt->sp = rt->stack_roots.count = rt->frame_count = 0;
    if (!rt_reclaim(rt)) return false;
    if (form->kind == AST_LIST && ast_symbol(form->as.list.first, "require")) {
        if (form->as.list.tail) return rt_error(rt, "require needs a proper specification list");
        if (!rt_require(rt, form->as.list.first->next)) return false;
        if (!rt_stack(rt, 1)) return false;
        rt->stack[0] = *result = V_NIL;
        rt->sp = rt->stack_roots.count = 1;
        return true;
    }
    if (form->kind == AST_LIST && ast_symbol(form->as.list.first, "ns")) {
        const Ast *name = form->as.list.first->next;
        if (form->as.list.tail || !name || name->kind != AST_SYMBOL) {
            rt->error_offset = form->offset;
            return rt_error(rt, "ns needs a namespace symbol");
        }
        if (rt->required_namespace != UINT32_MAX &&
            (strlen(rt->namespaces[rt->required_namespace]) != name->as.text.size ||
             memcmp(rt->namespaces[rt->required_namespace], name->as.text.data, name->as.text.size)))
            return rt_error(rt, "module namespace does not match required name");
        uint32_t space = rt_namespace(rt, name->as.text.data, name->as.text.size);
        if (space == UINT32_MAX) return false;
        rt->current_namespace = space;
        for (const Ast *clause = name->next; clause; clause = clause->next) {
            const Ast *head = clause->kind == AST_LIST ? clause->as.list.first : NULL;
            if (!head || clause->as.list.tail || head->kind != AST_KEYWORD ||
                head->as.text.size != 8 || memcmp(head->as.text.data, ":require", 8))
                return rt_error(rt, "ns only accepts require clauses");
            if (!rt_require(rt, head->next)) return false;
        }
        if (!rt_stack(rt, 1)) return false;
        rt->stack[0] = *result = V_NIL;
        rt->sp = rt->stack_roots.count = 1;
        return true;
    }
    Ast replacement, head;
    bool once = form->kind == AST_LIST && ast_symbol(form->as.list.first, "defonce");
    if (once) {
        const Ast *name = form->as.list.first->next;
        const Ast *value = name ? name->next : NULL;
        if (form->as.list.tail || !name || name->kind != AST_SYMBOL || !value ||
            (value->next && value->next->next)) {
            rt->error_offset = form->offset;
            return rt_error(rt, "defonce needs a name, optional docstring and value");
        }
        uint32_t global = rt_global(rt, name->as.text.data, name->as.text.size);
        if (global == UINT32_MAX) return false;
        if (rt->global_defined[global]) {
            if (!rt_stack(rt, 1)) return false;
            rt->stack[0] = *result = V_NIL;
            rt->sp = rt->stack_roots.count = 1;
            return true;
        }
        replacement = *form;
        head = *form->as.list.first;
        head.as.text.data = "def"; head.as.text.size = 3;
        replacement.as.list.first = &head;
        form = &replacement;
    }
    rt->compiling = true;
    ok = rt_compile(rt, form, &function);
    rt->compiling = false;
    for (size_t i = 0; i < rt->function_count; i++) {
        Function *f = rt->functions[i];
        if (!f || !f->compiling) continue;
        if (!ok || rt->error[0]) { rt_free_function(f); rt->functions[i] = NULL; }
        else f->compiling = false;
    }
    for (size_t i = 0; i < rt->constant_count; i++) if (rt->constant_state[i] == 2) {
        rt->constant_state[i] = ok && !rt->error[0] ? 1 : 0;
        if (!rt->constant_state[i]) rt->constants[i] = V_NIL;
    }
    rt->constant_free = rt->function_free = 0;
    if (!ok || rt->error[0]) return false;
    if (!rt_execute(rt, function, result)) return false;
    if (form->kind == AST_LIST) {
        const Ast *definition = form->as.list.first;
        if (ast_symbol(definition, "def") || ast_symbol(definition, "def-")) {
            const Ast *name = definition->next;
            if (name->kind == AST_LIST) name = name->as.list.first;
            uint32_t index = find_global(rt, rt->current_namespace, name->as.text.data, name->as.text.size);
            if (index != UINT32_MAX) rt->global_private[index] = ast_symbol(definition, "def-");
        }
    }
    if (once) rt->stack[0] = *result = V_NIL;
    return true;
}

bool runtime_eval(Runtime *rt, const char *source, size_t length, Value *result) {
    rt->error[0] = 0; rt->error_offset = 0;
    rt->sp = rt->stack_roots.count = rt->frame_count = 0;
    Reader reader;
    reader_init(&reader, source, length, 16 * 1024 * 1024);
    bool ok = true;
    Ast *form;
    while ((form = reader_next(&reader))) {
        if (!(ok = evaluate_top(rt, form, result))) break;
    }
    if (reader.error) {
        rt_error(rt, reader.error);
        rt->error_offset = reader.error_offset;
    }
    reader_destroy(&reader);
    if (!ok || rt->error[0]) {
        rt->sp = rt->stack_roots.count = rt->frame_count = 0;
        return false;
    }
    if (!rt->sp) {
        if (!rt_stack(rt, 1)) return false;
        rt->stack[0] = V_NIL;
        rt->sp = rt->stack_roots.count = 1;
    }
    *result = rt->stack[0];
    return true;
}

const char *runtime_error(const Runtime *rt) { return rt->error; }

bool runtime_call(Runtime *rt, const char *name, const Value *args, size_t count, Value *result) {
    rt->error[0] = 0; rt->error_offset = 0;
    rt->sp = rt->stack_roots.count = rt->frame_count = 0;
    uint32_t index = resolve_global(rt, name, strlen(name), false);
    if (index == UINT32_MAX || rt->globals[index] == V_UNBOUND) return rt_error(rt, "unbound global");
    if (count >= rt->stack_limit || !rt_stack(rt, count + 1)) return rt_error(rt, "VM stack limit exceeded");
    rt->stack[0] = rt->globals[index];
    if (count) memcpy(rt->stack + 1, args, count * sizeof(Value));
    rt->sp = rt->stack_roots.count = count + 1;
    if (!rt_invoke(rt, count, result)) {
        rt->sp = rt->stack_roots.count = rt->frame_count = 0;
        return false;
    }
    return true;
}
size_t runtime_error_offset(const Runtime *rt) { return rt->error_offset; }
size_t runtime_collections(const Runtime *rt) { return rt->heap.collections; }
size_t runtime_max_frames(const Runtime *rt) { return rt->max_frames; }
void runtime_gc_stress(Runtime *rt, bool enabled) { rt->stress_gc = enabled; }
void runtime_native(Runtime *rt, size_t threshold) { rt->native_threshold = threshold; }
size_t runtime_native_functions(const Runtime *rt) { return rt->native_functions; }
size_t runtime_native_entries(const Runtime *rt) { return rt->native_entries; }
size_t runtime_max_native_depth(const Runtime *rt) { return rt->max_native_depth; }
size_t runtime_native_scalar_entries(const Runtime *rt) { return rt->native_scalar_entries; }
size_t runtime_native_scalar_fallbacks(const Runtime *rt) { return rt->native_scalar_fallbacks; }
double runtime_native_compile_seconds(const Runtime *rt) { return rt->native_compile_seconds; }

static void print_value(FILE *out, Value v, unsigned depth) {
    if (depth > 100) { fputs("...", out); return; }
    if (value_is_int(v)) { fprintf(out, "%" PRIdPTR, value_to_int(v)); return; }
    if (v == V_NIL) fputs("nil", out);
    else if (v == V_FALSE) fputs("#f", out);
    else if (v == V_TRUE) fputs("#t", out);
    else if (v == V_EMPTY) fputs("()", out);
    else if ((v & 255) == TAG_CHAR) fprintf(out, "#\\x%" PRIxPTR, v >> 8);
    else if ((v & 255) == TAG_BUILTIN) fputs("#<builtin>", out);
    else if (rt_kind(v, K_FLOAT)) {
        double d; memcpy(&d, object_bytes(v), sizeof(d)); fprintf(out, "%.17g", d);
    } else if (rt_kind(v, K_STRING) || rt_kind(v, K_SYMBOL) || rt_kind(v, K_KEYWORD)) {
        size_t n; unsigned char *s = object_bytes(v); memcpy(&n, s, sizeof(n));
        fwrite(s + sizeof(n), 1, n, out);
    } else if (rt_kind(v, K_ARRAY)) {
        ArrayInfo *info = object_bytes(v);
        Value *items = info->length ? object_fields(object_fields(v)[0]) : NULL;
        fputc('[', out);
        for (size_t i = 0; i < info->length; i++) {
            if (i) fputc(' ', out);
            print_value(out, items[i], depth + 1);
        }
        fputc(']', out);
    } else if (rt_kind(v, K_TABLE)) {
        TableInfo *info = object_bytes(v);
        Value *entries = info->used ? object_fields(object_fields(v)[0]) : NULL;
        fputc('{', out);
        size_t printed = 0;
        for (size_t i = 0; i < info->used; i++) if (entries[2*i+1] != V_NIL) {
            if (printed++) fputc(' ', out);
            print_value(out, entries[2*i], depth + 1);
            fputc(' ', out);
            print_value(out, entries[2*i+1], depth + 1);
        }
        fputc('}', out);
    } else if (rt_kind(v, K_PAIR)) {
        fputc('(', out);
        unsigned count = 0;
        while (rt_kind(v, K_PAIR) && count++ < 1000) {
            if (count > 1) fputc(' ', out);
            print_value(out, object_fields(v)[0], depth + 1);
            v = object_fields(v)[1];
        }
        if (v != V_EMPTY) { fputs(" . ", out); print_value(out, v, depth + 1); }
        fputc(')', out);
    } else if (rt_kind(v, K_POINTER)) fputs("#<foreign-pointer>", out);
    else if (rt_kind(v, K_FOREIGN_LIBRARY)) fputs("#<foreign-library>", out);
    else fputs("#<function>", out);
}

void runtime_print(Runtime *rt, FILE *out, Value value) {
    (void)rt;
    print_value(out, value, 0);
}
