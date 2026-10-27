#include "internal.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  uint32_t space;
  unsigned state;
} oti_module;

typedef struct {
  uint32_t owner, target;
  char* name;
  bool alias;
} oti_import;

struct oti_module_state {
  char** paths;
  size_t path_count, path_capacity;
  oti_module* items;
  size_t count, capacity, depth, source_bytes;
  oti_import* imports;
  size_t import_count, import_capacity;
};

static oti_module_state* state(ot_state* rt) {
  if (!rt->modules) {
    rt->modules = calloc(1, sizeof(oti_module_state));
    if (!rt->modules) oti_error(rt, "out of host memory");
  }
  return rt->modules;
}

static char* copy_text(ot_state* rt, const char* s, size_t n) {
  char* copy = malloc(n + 1);
  if (!copy) {
    oti_error(rt, "out of host memory");
    return NULL;
  }
  memcpy(copy, s, n);
  copy[n] = 0;
  return copy;
}

bool ot_add_load_path(ot_state* rt, const char* path) {
  if (!oti_host_entry(rt)) return false;
  oti_module_state* m = state(rt);
  if (!m || !oti_grow(rt, (void**)&m->paths, &m->path_capacity, m->path_count + 1, sizeof(char*)))
    return false;
  char* copy = copy_text(rt, path, strlen(path));
  if (!copy) return false;
  m->paths[m->path_count++] = copy;
  return true;
}

bool ot_add_project_paths(ot_state* rt, const char* project_file) {
  if (!oti_host_entry(rt)) return false;
  FILE* file = fopen(project_file, "rb");
  if (!file) return oti_error(rt, "cannot open project file");
  size_t length;
  char* source = oti_read_file(rt, file, OTI_SOURCE_LIMIT, "project file", &length);
  if (!source) return false;
  const char* slash = strrchr(project_file, '/');
  size_t prefix = slash ? (size_t)(slash - project_file + 1) : 0;
  oti_reader reader;
  oti_reader_init(&reader, source, length, OTI_SOURCE_LIMIT);
  bool ok = true;
  oti_ast* form;
  while (ok && (form = oti_reader_next(&reader))) {
    if (form->kind != AST_LIST || !oti_ast_symbol(form->as.list.first, "paths")) continue;
    if (form->as.list.tail) {
      ok = oti_error(rt, "project paths must be a proper list");
      break;
    }
    for (oti_ast* entry = form->as.list.first->next; ok && entry; entry = entry->next) {
      if (entry->kind != AST_STRING || memchr(entry->as.text.data, 0, entry->as.text.size)) {
        ok = oti_error(rt, "project paths must be strings without NUL");
        break;
      }
      const char* text = entry->as.text.data;
      size_t size = entry->as.text.size;
      size_t base = size && text[0] == '/' ? 0 : prefix;
      char* directory = malloc(base + size + 1);
      if (!directory) {
        ok = oti_error(rt, "out of host memory");
        break;
      }
      memcpy(directory, project_file, base);
      memcpy(directory + base, text, size);
      directory[base + size] = 0;
      ok = ot_add_load_path(rt, directory);
      free(directory);
    }
  }
  if (reader.error) ok = oti_coded_error(rt, reader.error_code, reader.error);
  oti_reader_destroy(&reader);
  free(source);
  return ok;
}

void oti_modules_destroy(ot_state* rt) {
  oti_module_state* m = rt->modules;
  if (!m) return;
  for (size_t i = 0; i < m->path_count; i++) free(m->paths[i]);
  for (size_t i = 0; i < m->import_count; i++) free(m->imports[i].name);
  free(m->paths);
  free(m->items);
  free(m->imports);
  free(m);
}

static uint32_t lookup_import(ot_state* rt, uint32_t owner, const char* name, size_t length,
                              bool alias) {
  oti_module_state* m = rt->modules;
  if (m)
    for (size_t i = 0; i < m->import_count; i++) {
      oti_import* in = &m->imports[i];
      if (in->owner == owner && in->alias == alias && strlen(in->name) == length &&
          !memcmp(in->name, name, length))
        return in->target;
    }
  return UINT32_MAX;
}

uint32_t oti_import_global_in(ot_state* rt, uint32_t space, const char* name, size_t length) {
  return lookup_import(rt, space, name, length, false);
}

uint32_t oti_import_global(ot_state* rt, const char* name, size_t length) {
  return lookup_import(rt, rt->current_namespace, name, length, false);
}

uint32_t oti_alias_namespace(ot_state* rt, const char* name, size_t length) {
  return lookup_import(rt, rt->current_namespace, name, length, true);
}

static bool install_import(ot_state* rt, const oti_ast* name, uint32_t target, bool alias) {
  oti_module_state* m = state(rt);
  if (!m) return false;
  for (size_t i = 0; i < m->import_count; i++) {
    oti_import* in = &m->imports[i];
    if (in->owner == rt->current_namespace && in->alias == alias &&
        strlen(in->name) == name->as.text.size &&
        !memcmp(in->name, name->as.text.data, name->as.text.size)) {
      in->target = target;
      return true;
    }
  }
  if (!oti_grow(rt, (void**)&m->imports, &m->import_capacity, m->import_count + 1,
                sizeof(oti_import)))
    return false;
  char* copy = copy_text(rt, name->as.text.data, name->as.text.size);
  if (!copy) return false;
  m->imports[m->import_count++] = (oti_import){rt->current_namespace, target, copy, alias};
  return true;
}

static bool load_module(ot_state* rt, uint32_t space, bool reload, bool legacy_ffi) {
  oti_module_state* m = state(rt);
  if (!m) return false;
  const char* name = rt->namespaces[space];
  if (!strcmp(name, "otium.core") || (!strcmp(name, "otium.ffi") && legacy_ffi) ||
      !strcmp(name, "otium.process") || !strcmp(name, "otium.reader") ||
      !strcmp(name, "otium.generic"))
    return true;
  size_t index = 0;
  while (index < m->count && m->items[index].space != space) index++;
  if (index == m->count) {
    if (!oti_grow(rt, (void**)&m->items, &m->capacity, index + 1, sizeof(oti_module))) return false;
    m->items[m->count++] = (oti_module){space, 0};
  }
  if (m->items[index].state == 1) return oti_coded_error(rt, "E0205", "circular module require");
  if (m->items[index].state == 2 && !reload) return true;
  m->items[index].state = 0;
  if (m->depth >= 128) return oti_error(rt, "module nesting limit exceeded");
  const char* defaults[] = {".", "lib"};
  size_t paths = m->path_count + 2;
  FILE* file = NULL;
  for (size_t i = 0; i < paths && !file; i++) {
    const char* directory = i < m->path_count ? m->paths[i] : defaults[i - m->path_count];
    size_t d = strlen(directory), n = strlen(name);
    if (d > SIZE_MAX - n - 6) return oti_error(rt, "module path too long");
    char* path = malloc(d + n + 6);
    if (!path) return oti_error(rt, "out of host memory");
    memcpy(path, directory, d);
    path[d] = '/';
    for (size_t j = 0; j < n; j++) path[d + 1 + j] = name[j] == '.' ? '/' : name[j];
    const char* suffixes[] = {".ot", ".scm"};
    for (size_t suffix = 0; suffix < 2 && !file; suffix++) {
      strcpy(path + d + n + 1, suffixes[suffix]);
      file = fopen(path, "rb");
      if (!file && errno != ENOENT && errno != ENOTDIR) {
        free(path);
        return oti_error(rt, "cannot open module file");
      }
    }
    free(path);
  }
  if (!file) return oti_coded_error(rt, "E0202", "module file not found");
  size_t bytes;
  char* source = oti_read_file(rt, file, OTI_SOURCE_LIMIT - m->source_bytes, "module file", &bytes);
  if (!source) return false;
  uint32_t caller = rt->current_namespace, expected = rt->required_namespace;
  rt->current_namespace = rt->required_namespace = space;
  m->items[index].state = 1;
  ot_value ignored;
  m->depth++;
  m->source_bytes += bytes;
  bool ok = ot_eval(rt, source, bytes, &ignored);
  m->depth--;
  m->source_bytes -= bytes;
  /* Recursive requires may grow the module array. Keep only its index. */
  m->items[index].state = ok ? 2 : 0;
  rt->current_namespace = caller;
  rt->required_namespace = expected;
  free(source);
  return ok;
}

static bool keyword(const oti_ast* a, const char* name) {
  return a && a->kind == AST_KEYWORD && a->as.text.size == strlen(name) &&
         !memcmp(a->as.text.data, name, a->as.text.size);
}

static bool one_require(ot_state* rt, const oti_ast** cursor) {
  const oti_ast* module = *cursor;
  if (!module || module->kind != AST_SYMBOL) return oti_error(rt, "require needs a module name");
  const oti_ast *p = module->next, *alias = NULL, *refer = NULL;
  bool reload = false;
  while (p && p->kind == AST_KEYWORD) {
    if (keyword(p, ":reload")) {
      reload = true;
      p = p->next;
    } else if (keyword(p, ":as")) {
      if (alias || !p->next || p->next->kind != AST_SYMBOL)
        return oti_error(rt, "require :as needs one alias");
      alias = p->next;
      p = alias->next;
    } else if (keyword(p, ":refer")) {
      if (refer || !p->next || p->next->kind != AST_LIST || p->next->as.list.tail)
        return oti_error(rt, "require :refer needs a name list");
      refer = p->next;
      p = refer->next;
    } else return oti_error(rt, "unknown require option");
  }
  *cursor = p;
  uint32_t space = oti_namespace(rt, module->as.text.data, module->as.text.size);
  if (space == UINT32_MAX) return false;
  if (alias)
    for (size_t i = 0; i < rt->namespace_count; i++)
      if (i != space && strlen(rt->namespaces[i]) == alias->as.text.size &&
          !memcmp(rt->namespaces[i], alias->as.text.data, alias->as.text.size))
        return oti_coded_error(rt, "E0208", "require alias conflicts with a namespace name");
  if (!load_module(rt, space, reload, oti_ast_symbol(module, "ffi"))) {
    oti_error(rt, "");
    oti_error_record* e = &rt->error;
    if (e->in_module) return false;
    e->in_module = true;
    size_t length = strlen(rt->namespaces[space]) + strlen(e->message) + sizeof("module []: ");
    char* message = malloc(length);
    if (!message) return false;
    snprintf(message, length, "module [%s]: %s", rt->namespaces[space], e->message);
    if (e->owned) free(e->message);
    e->message = message;
    e->owned = true;
    return false;
  }
  if (refer)
    for (const oti_ast* name = refer->as.list.first; name; name = name->next) {
      if (name->kind != AST_SYMBOL) return oti_error(rt, "require :refer needs symbol names");
      uint32_t target = UINT32_MAX;
      for (size_t i = 0; i < rt->global_count; i++)
        if (rt->global_info[i].space == space &&
            strlen(rt->global_info[i].name) == name->as.text.size &&
            !memcmp(rt->global_info[i].name, name->as.text.data, name->as.text.size))
          target = (uint32_t)i;
      /* otium.generic's names are core bindings. */
      if (target == UINT32_MAX && !strcmp(rt->namespaces[space], "otium.generic"))
        target = oti_binding_global(rt, 0, name->as.text.data, name->as.text.size, false);
      for (size_t i = 0; i < rt->global_count; i++)
        if (rt->global_info[i].space == rt->current_namespace &&
            strlen(rt->global_info[i].name) == name->as.text.size &&
            !memcmp(rt->global_info[i].name, name->as.text.data, name->as.text.size))
          return oti_error(rt, "referred import conflicts with a local binding");
      if (target == UINT32_MAX || rt->globals[target] == V_UNBOUND ||
          rt->global_info[target].is_private)
        return oti_coded_error(
            rt, target != UINT32_MAX && rt->global_info[target].is_private ? "E0204" : "E0203",
            "cannot refer missing or private module binding");
      if (!install_import(rt, name, target, false)) return false;
    }
  if (alias)
    for (size_t i = 0; i < rt->namespace_count; i++)
      if (i != space && strlen(rt->namespaces[i]) == alias->as.text.size &&
          !memcmp(rt->namespaces[i], alias->as.text.data, alias->as.text.size))
        return oti_coded_error(rt, "E0208", "require alias conflicts with a namespace name");
  return !alias || install_import(rt, alias, space, true);
}

bool oti_require(ot_state* rt, const oti_ast* specs) {
  if (!specs) return true;
  const oti_ast* p = specs;
  while (p) {
    if (p->kind == AST_LIST) {
      const oti_ast* group = p;
      if (oti_ast_symbol(group->as.list.first, "quote")) {
        const oti_ast* quoted = group->as.list.first->next;
        if (group->as.list.tail || !quoted || quoted->next ||
            (quoted->kind != AST_LIST && quoted->kind != AST_SYMBOL))
          return oti_error(rt, "invalid quoted require specification");
        if (quoted->kind == AST_SYMBOL) {
          oti_ast module = *quoted;
          module.next = NULL;
          const oti_ast* inside = &module;
          if (!one_require(rt, &inside)) return false;
          p = p->next;
          continue;
        }
        group = quoted;
      }
      const oti_ast* inside = group->as.list.first;
      if (group->as.list.tail || !one_require(rt, &inside) || inside)
        return oti_error(rt, "invalid grouped require specification");
      p = p->next;
    } else if (!one_require(rt, &p)) return false;
  }
  return true;
}
