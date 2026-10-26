#include "internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint32_t space; unsigned state; } Module;
typedef struct { uint32_t owner, target; char *name; bool alias; } Import;
struct ModuleState {
    char **paths;
    size_t path_count, path_capacity;
    Module *items;
    size_t count, capacity, depth, source_bytes;
    Import *imports;
    size_t import_count, import_capacity;
};

static ModuleState *state(Runtime *rt) {
    if (!rt->modules) {
        rt->modules = calloc(1, sizeof(ModuleState));
        if (!rt->modules) rt_error(rt, "out of host memory");
    }
    return rt->modules;
}
static char *copy_text(Runtime *rt, const char *s, size_t n) {
    char *copy = malloc(n + 1);
    if (!copy) { rt_error(rt, "out of host memory"); return NULL; }
    memcpy(copy, s, n); copy[n] = 0;
    return copy;
}
bool runtime_load_path(Runtime *rt, const char *path) {
    ModuleState *m = state(rt);
    if (!m || !rt_grow(rt, (void **)&m->paths, &m->path_capacity, m->path_count + 1, sizeof(char *))) return false;
    char *copy = copy_text(rt, path, strlen(path));
    if (!copy) return false;
    m->paths[m->path_count++] = copy;
    return true;
}
void rt_modules_destroy(Runtime *rt) {
    ModuleState *m = rt->modules;
    if (!m) return;
    for (size_t i = 0; i < m->path_count; i++) free(m->paths[i]);
    for (size_t i = 0; i < m->import_count; i++) free(m->imports[i].name);
    free(m->paths); free(m->items); free(m->imports); free(m);
}
static uint32_t lookup_import(Runtime *rt, uint32_t owner, const char *name, size_t length, bool alias) {
    ModuleState *m = rt->modules;
    if (m) for (size_t i = 0; i < m->import_count; i++) {
        Import *in = &m->imports[i];
        if (in->owner == owner && in->alias == alias &&
            strlen(in->name) == length && !memcmp(in->name, name, length)) return in->target;
    }
    return UINT32_MAX;
}
uint32_t rt_import_global_in(Runtime *rt, uint32_t space, const char *name, size_t length) {
    return lookup_import(rt, space, name, length, false);
}
uint32_t rt_import_global(Runtime *rt, const char *name, size_t length) {
    return lookup_import(rt, rt->current_namespace, name, length, false);
}
uint32_t rt_alias_namespace(Runtime *rt, const char *name, size_t length) {
    return lookup_import(rt, rt->current_namespace, name, length, true);
}
static bool install_import(Runtime *rt, const Ast *name, uint32_t target, bool alias) {
    ModuleState *m = state(rt);
    if (!m) return false;
    for (size_t i = 0; i < m->import_count; i++) {
        Import *in = &m->imports[i];
        if (in->owner == rt->current_namespace && in->alias == alias &&
            strlen(in->name) == name->as.text.size && !memcmp(in->name, name->as.text.data, name->as.text.size)) {
            in->target = target;
            return true;
        }
    }
    if (!rt_grow(rt, (void **)&m->imports, &m->import_capacity, m->import_count + 1, sizeof(Import))) return false;
    char *copy = copy_text(rt, name->as.text.data, name->as.text.size);
    if (!copy) return false;
    m->imports[m->import_count++] = (Import){rt->current_namespace, target, copy, alias};
    return true;
}
static bool load_module(Runtime *rt, uint32_t space, bool reload, bool legacy_ffi) {
    ModuleState *m = state(rt);
    if (!m) return false;
    const char *name = rt->namespaces[space];
    if (!strcmp(name, "otium.core") || (!strcmp(name, "otium.ffi") && legacy_ffi) || !strcmp(name, "otium.process") || !strcmp(name,"otium.reader")) return true;
    size_t index = 0;
    while (index < m->count && m->items[index].space != space) index++;
    if (index == m->count) {
        if (!rt_grow(rt, (void **)&m->items, &m->capacity, index + 1, sizeof(Module))) return false;
        m->items[m->count++] = (Module){space, 0};
    }
    if (m->items[index].state == 1) return rt_error(rt, "circular module require");
    if (m->items[index].state == 2 && !reload) return true;
    m->items[index].state = 0;
    if (m->depth >= 128) return rt_error(rt, "module nesting limit exceeded");
    const char *defaults[] = {".", "lib"};
    size_t paths = m->path_count + 2;
    FILE *file = NULL;
    for (size_t i = 0; i < paths && !file; i++) {
        const char *directory = i < m->path_count ? m->paths[i] : defaults[i - m->path_count];
        size_t d = strlen(directory), n = strlen(name);
        if (d > SIZE_MAX - n - 6) return rt_error(rt, "module path too long");
        char *path = malloc(d + n + 6);
        if (!path) return rt_error(rt, "out of host memory");
        memcpy(path, directory, d); path[d] = '/';
        for (size_t j = 0; j < n; j++) path[d + 1 + j] = name[j] == '.' ? '/' : name[j];
        const char *suffixes[] = {".ot", ".scm"};
        for (size_t suffix = 0; suffix < 2 && !file; suffix++) {
            strcpy(path + d + n + 1, suffixes[suffix]);
            file = fopen(path, "rb");
            if (!file && errno != ENOENT && errno != ENOTDIR) {
                free(path); return rt_error(rt, "cannot open module file");
            }
        }
        free(path);
    }
    if (!file) return rt_error(rt, "module file not found");
    bool ok = !fseek(file, 0, SEEK_END);
    long bytes = ok ? ftell(file) : -1;
    if (bytes < 0 || (unsigned long)bytes > 16 * 1024 * 1024 - m->source_bytes) {
        fclose(file); return rt_error(rt, "module file exceeds source budget or cannot be read");
    }
    rewind(file);
    char *source = malloc((size_t)bytes + 1);
    if (!source) { fclose(file); return rt_error(rt, "out of host memory"); }
    ok = fread(source, 1, (size_t)bytes, file) == (size_t)bytes && !ferror(file);
    fclose(file); source[bytes] = 0;
    if (!ok) { free(source); return rt_error(rt, "cannot read module file"); }
    uint32_t caller = rt->current_namespace, expected = rt->required_namespace;
    rt->current_namespace = rt->required_namespace = space;
    m->items[index].state = 1;
    Value ignored;
    m->depth++; m->source_bytes += (size_t)bytes;
    ok = runtime_eval(rt, source, (size_t)bytes, &ignored);
    m->depth--; m->source_bytes -= (size_t)bytes;
    /* Recursive requires may grow the module array. Keep only its index. */
    m->items[index].state = ok ? 2 : 0;
    rt->current_namespace = caller; rt->required_namespace = expected;
    free(source);
    return ok;
}
static bool keyword(const Ast *a, const char *name) {
    return a && a->kind == AST_KEYWORD && a->as.text.size == strlen(name) &&
        !memcmp(a->as.text.data, name, a->as.text.size);
}
static bool one_require(Runtime *rt, const Ast **cursor) {
    const Ast *module = *cursor;
    if (!module || module->kind != AST_SYMBOL) return rt_error(rt, "require needs a module name");
    const Ast *p = module->next, *alias = NULL, *refer = NULL;
    bool reload = false;
    while (p && p->kind == AST_KEYWORD) {
        if (keyword(p, ":reload")) { reload = true; p = p->next; }
        else if (keyword(p, ":as")) {
            if (alias || !p->next || p->next->kind != AST_SYMBOL) return rt_error(rt, "require :as needs one alias");
            alias = p->next; p = alias->next;
        } else if (keyword(p, ":refer")) {
            if (refer || !p->next || p->next->kind != AST_LIST || p->next->as.list.tail)
                return rt_error(rt, "require :refer needs a name list");
            refer = p->next; p = refer->next;
        } else return rt_error(rt, "unknown require option");
    }
    *cursor = p;
    uint32_t space = rt_namespace(rt, module->as.text.data, module->as.text.size);
    if (space == UINT32_MAX) return false;
    if (alias) for (size_t i = 0; i < rt->namespace_count; i++)
        if (i != space && strlen(rt->namespaces[i]) == alias->as.text.size &&
            !memcmp(rt->namespaces[i], alias->as.text.data, alias->as.text.size))
            return rt_error(rt, "require alias conflicts with a namespace name");
    if (!load_module(rt, space, reload, ast_symbol(module,"ffi"))) {
        if (!strncmp(rt->error, "module [", 8)) return false;
        char cause[192];
        memcpy(cause, rt->error, sizeof(cause)); cause[sizeof(cause)-1] = 0;
        snprintf(rt->error, sizeof(rt->error), "module [%.64s]: %.100s", rt->namespaces[space], cause);
        return false;
    }
    if (refer) for (const Ast *name = refer->as.list.first; name; name = name->next) {
        if (name->kind != AST_SYMBOL) return rt_error(rt, "require :refer needs symbol names");
        uint32_t target = UINT32_MAX;
        for (size_t i = 0; i < rt->global_count; i++)
            if (rt->global_namespaces[i] == space && strlen(rt->names[i]) == name->as.text.size &&
                !memcmp(rt->names[i], name->as.text.data, name->as.text.size)) target = (uint32_t)i;
        for (size_t i = 0; i < rt->global_count; i++)
            if (rt->global_namespaces[i] == rt->current_namespace && strlen(rt->names[i]) == name->as.text.size &&
                !memcmp(rt->names[i], name->as.text.data, name->as.text.size))
                return rt_error(rt, "referred import conflicts with a local binding");
        if (target == UINT32_MAX || rt->globals[target] == V_UNBOUND || rt->global_private[target])
            return rt_error(rt, "cannot refer missing or private module binding");
        if (!install_import(rt, name, target, false)) return false;
    }
    if (alias) for (size_t i = 0; i < rt->namespace_count; i++)
        if (i != space && strlen(rt->namespaces[i]) == alias->as.text.size &&
            !memcmp(rt->namespaces[i], alias->as.text.data, alias->as.text.size))
            return rt_error(rt, "require alias conflicts with a namespace name");
    return !alias || install_import(rt, alias, space, true);
}
bool rt_require(Runtime *rt, const Ast *specs) {
    if (!specs) return true;
    const Ast *p = specs;
    while (p) {
        if (p->kind == AST_LIST) {
            const Ast *group = p;
            if (ast_symbol(group->as.list.first, "quote")) {
                const Ast *quoted = group->as.list.first->next;
                if (group->as.list.tail || !quoted || quoted->next ||
                    (quoted->kind != AST_LIST && quoted->kind != AST_SYMBOL))
                    return rt_error(rt, "invalid quoted require specification");
                if (quoted->kind == AST_SYMBOL) {
                    Ast module = *quoted;
                    module.next = NULL;
                    const Ast *inside = &module;
                    if (!one_require(rt, &inside)) return false;
                    p = p->next;
                    continue;
                }
                group = quoted;
            }
            const Ast *inside = group->as.list.first;
            if (group->as.list.tail || !one_require(rt, &inside) || inside)
                return rt_error(rt, "invalid grouped require specification");
            p = p->next;
        } else if (!one_require(rt, &p)) return false;
    }
    return true;
}
