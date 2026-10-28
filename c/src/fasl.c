#include "internal.h"

/* Loading fasls and boot files in native processes. The format is described
 * in lib/otium/compiler/fasl.ot, which writes them: units to install and
 * run, and directives that replay what the evaluator does around them. A
 * boot file is a fasl whose main entries load the expander and core, with
 * module sections that require loads instead of source. */

#ifdef OTIUM_NATIVE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
  F_END,
  F_UNIT,
  F_FORM,
  F_MACRO,
  F_DOC,
  F_DEFONCE,
  F_MODULE,
  F_END_MODULE,
  F_NAMESPACE,
  F_SOURCE,
};

enum {
  D_NIL,
  D_TRUE,
  D_FALSE,
  D_EMPTY,
  D_INT,
  D_FLOAT,
  D_CHAR,
  D_STRING,
  D_SYMBOL,
  D_GENSYM,
  D_KEYWORD,
  D_LIST,
  D_ARRAY,
  D_BYTES,
  D_CELL,
  D_EOF,
  D_OBJECT_CLASS,
};

typedef struct {
  char* name;
  size_t start, end;
} oti_boot_section;

struct oti_boot {
  unsigned char* bytes;
  size_t length;
  oti_boot_section* modules;
  size_t module_count;
};

typedef struct {
  ot_state* rt;
  const unsigned char *base, *p, *end;
  bool bad;
  /* Gensyms read so far, by serial, so one fasl's uses of a gensym are one
   * symbol. Pairs of (serial, symbol) on rt->stack would move; keep them in
   * a rooted array instead. */
  ot_value* gensyms;
  uint64_t* serials;
  size_t gensym_count, gensym_capacity, serial_capacity;
  oti_roots gensym_roots;
} oti_fasl;

static uint64_t f_unsigned(oti_fasl* f) {
  uint64_t value = 0;
  for (unsigned shift = 0; shift < 64; shift += 7) {
    if (f->p >= f->end) break;
    unsigned char byte = *f->p++;
    value |= (uint64_t)(byte & 127) << shift;
    if (!(byte & 128)) return value;
  }
  f->bad = true;
  return 0;
}

static int64_t f_signed(oti_fasl* f) {
  uint64_t value = 0;
  for (unsigned shift = 0; shift < 70; shift += 7) {
    if (f->p >= f->end) break;
    unsigned char byte = *f->p++;
    if (shift < 64) value |= (uint64_t)(byte & 127) << shift;
    if (!(byte & 128)) {
      if (shift + 7 < 64 && (byte & 64)) value |= ~(uint64_t)0 << (shift + 7);
      return (int64_t)value;
    }
  }
  f->bad = true;
  return 0;
}

static const char* f_text(oti_fasl* f, size_t* length) {
  *length = (size_t)f_unsigned(f);
  if (f->bad || *length > (size_t)(f->end - f->p)) {
    f->bad = true;
    *length = 0;
    return "";
  }
  const char* text = (const char*)f->p;
  f->p += *length;
  return text;
}

static bool push(ot_state* rt, ot_value v) {
  if (!v || !oti_stack(rt, rt->sp + 1)) return false;
  rt->stack[rt->sp] = v;
  rt->sp = rt->stack_roots.count = rt->sp + 1;
  return true;
}

static ot_value pop(ot_state* rt) {
  rt->sp = rt->stack_roots.count = rt->sp - 1;
  return rt->stack[rt->sp];
}

static ot_value gensym(oti_fasl* f, const char* name, size_t length, uint64_t serial) {
  ot_state* rt = f->rt;
  for (size_t i = 0; i < f->gensym_count; i++)
    if (f->serials[i] == serial) return f->gensyms[i];
  ot_value symbol = oti_text(rt, K_SYMBOL, name, length);
  if (!symbol) return 0;
  oti_symbol_set_id(symbol, ++rt->gensym_counter);
  if (!oti_grow(rt, (void**)&f->serials, &f->serial_capacity, f->gensym_count + 1,
                sizeof(uint64_t)))
    return 0;
  size_t old = f->gensym_capacity;
  if (!oti_grow(rt, (void**)&f->gensyms, &f->gensym_capacity, f->gensym_count + 1,
                sizeof(ot_value)))
    return 0;
  for (size_t i = old; i < f->gensym_capacity; i++) f->gensyms[i] = OT_NIL;
  f->gensym_roots.slots = f->gensyms;
  f->gensym_roots.count = f->gensym_capacity;
  f->serials[f->gensym_count] = serial;
  f->gensyms[f->gensym_count++] = symbol;
  return symbol;
}

/* Read a datum and push it on rt->stack. */
static bool datum(oti_fasl* f, unsigned depth) {
  ot_state* rt = f->rt;
  if (f->bad || f->p >= f->end || depth > 10000) return f->bad = true, false;
  unsigned tag = *f->p++;
  size_t length;
  const char* text;
  switch (tag) {
    case D_NIL: return push(rt, OT_NIL);
    case D_TRUE: return push(rt, OT_TRUE);
    case D_FALSE: return push(rt, OT_FALSE);
    case D_EMPTY: return push(rt, OT_EMPTY);
    case D_EOF: return push(rt, OT_EOF);
    case D_INT: return push(rt, ot_int((intptr_t)f_signed(f)));
    case D_CHAR: return push(rt, ((ot_value)f_unsigned(f) << 8) | TAG_CHAR);
    case D_FLOAT: {
      text = f_text(f, &length);
      char buffer[64];
      if (length >= sizeof(buffer)) return f->bad = true, false;
      memcpy(buffer, text, length);
      buffer[length] = 0;
      return push(rt, oti_float(rt, strtod(buffer, NULL)));
    }
    case D_STRING: text = f_text(f, &length); return push(rt, oti_text(rt, K_STRING, text, length));
    case D_SYMBOL: text = f_text(f, &length); return push(rt, oti_intern(rt, K_SYMBOL, text, length));
    case D_GENSYM: {
      text = f_text(f, &length);
      return push(rt, gensym(f, text, length, f_unsigned(f)));
    }
    case D_KEYWORD: {
      text = f_text(f, &length);
      char* name = malloc(length + 2);
      if (!name) return oti_error(rt, "out of host memory");
      name[0] = ':';
      memcpy(name + 1, text, length);
      ot_value keyword = oti_intern(rt, K_KEYWORD, name, length + 1);
      free(name);
      return push(rt, keyword);
    }
    case D_LIST: {
      size_t count = (size_t)f_unsigned(f);
      for (size_t i = 0; i < count; i++)
        if (!datum(f, depth + 1)) return false;
      if (!datum(f, depth + 1)) return false;
      for (size_t i = 0; i < count; i++) {
        ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
        if (!pair) return false;
        oti_as_pair(pair)->cdr = pop(rt);
        oti_as_pair(pair)->car = pop(rt);
        if (!push(rt, pair)) return false;
      }
      return true;
    }
    case D_ARRAY: {
      size_t count = (size_t)f_unsigned(f), base = rt->sp;
      for (size_t i = 0; i < count; i++)
        if (!datum(f, depth + 1)) return false;
      ot_value array = oti_array_from(rt, base, count);
      rt->sp = rt->stack_roots.count = base;
      return push(rt, array);
    }
    case D_BYTES: {
      text = f_text(f, &length);
      ot_value bytes = oti_bytes_new(rt, length);
      if (bytes) memcpy(oti_bytes_data(bytes), text, length);
      return push(rt, bytes);
    }
    case D_CELL: {
      size_t space_length, name_length;
      const char* space_name = f_text(f, &space_length);
      const char* name = f_text(f, &name_length);
      uint32_t space = oti_namespace(rt, space_name, space_length);
      if (space == UINT32_MAX) return false;
      uint32_t cell = oti_binding_global(rt, space, name, name_length, true);
      if (cell == UINT32_MAX) return false;
      return push(rt, ot_int((intptr_t)cell));
    }
    case D_OBJECT_CLASS:
      if (!oti_classes_init(rt)) return false;
      return push(rt, rt->classes[C_OBJECT]);
    default: return f->bad = true, false;
  }
}

static bool fasl_error(oti_fasl* f) {
  if (!oti_failed(f->rt)) oti_error(f->rt, "malformed fasl");
  return false;
}

/* Skip one datum without building it. */
static bool skip_datum(oti_fasl* f) {
  if (f->p >= f->end) return f->bad = true, false;
  unsigned tag = *f->p++;
  size_t length;
  switch (tag) {
    case D_NIL:
    case D_TRUE:
    case D_FALSE:
    case D_EMPTY:
    case D_EOF:
    case D_OBJECT_CLASS: return true;
    case D_INT: f_signed(f); return !f->bad;
    case D_CHAR: f_unsigned(f); return !f->bad;
    case D_FLOAT:
    case D_STRING:
    case D_SYMBOL:
    case D_KEYWORD:
    case D_BYTES: f_text(f, &length); return !f->bad;
    case D_GENSYM: f_text(f, &length); f_unsigned(f); return !f->bad;
    case D_CELL: f_text(f, &length); f_text(f, &length); return !f->bad;
    case D_LIST: {
      size_t count = (size_t)f_unsigned(f);
      for (size_t i = 0; i <= count; i++)
        if (!skip_datum(f)) return false;
      return true;
    }
    case D_ARRAY: {
      size_t count = (size_t)f_unsigned(f);
      for (size_t i = 0; i < count; i++)
        if (!skip_datum(f)) return false;
      return true;
    }
    default: return f->bad = true, false;
  }
}

/* Skip one entry; a module section is skipped whole. */
static bool skip_entry(oti_fasl* f) {
  unsigned tag = *f->p++;
  switch (tag) {
    case F_UNIT:
    case F_FORM:
    case F_DEFONCE:
    case F_NAMESPACE:
    case F_SOURCE: return skip_datum(f);
    case F_MACRO: return skip_datum(f) && skip_datum(f);
    case F_DOC: return skip_datum(f) && skip_datum(f) && skip_datum(f);
    case F_END_MODULE: return true;
    case F_MODULE:
      if (!skip_datum(f)) return false;
      while (f->p < f->end && *f->p != F_END_MODULE)
        if (!skip_entry(f)) return false;
      if (f->p >= f->end) return f->bad = true, false;
      f->p++;
      return true;
    default: return f->bad = true, false;
  }
}

static uint32_t current_global(ot_state* rt, ot_value name) {
  size_t length;
  const char* text = oti_text_bytes(name, &length);
  return oti_binding_global(rt, rt->current_namespace, text, length, true);
}

static bool document_global(ot_state* rt, uint32_t cell, ot_value doc) {
  char* text = NULL;
  size_t length = 0;
  if (oti_kind(doc, K_STRING)) {
    const char* bytes = oti_text_bytes(doc, &length);
    text = malloc(length + 1);
    if (!text) return oti_error(rt, "out of host memory");
    memcpy(text, bytes, length);
    text[length] = 0;
  }
  free(rt->global_info[cell].doc.text);
  rt->global_info[cell].doc = (oti_documentation){text, length};
  return true;
}

/* Run entries until the end of the fasl or of the module section. */
static bool run(oti_fasl* f, bool in_module) {
  ot_state* rt = f->rt;
  bool skip = false;
  for (;;) {
    if (f->bad || f->p >= f->end) return fasl_error(f);
    unsigned tag = *f->p;
    if (tag == F_END) return !in_module || fasl_error(f);
    if (tag == F_END_MODULE) {
      f->p++;
      return in_module || fasl_error(f);
    }
    if (tag == F_MODULE || skip) {
      if (!skip_entry(f)) return fasl_error(f);
      skip = false;
      continue;
    }
    f->p++;
    size_t base = rt->sp;
    bool ok = true;
    switch (tag) {
      case F_UNIT: {
        if (!datum(f, 0)) return f->bad ? fasl_error(f) : false;
        ot_value thunk = oti_native_install(rt, rt->sp - 1);
        rt->sp = rt->stack_roots.count = base;
        ok = thunk && oti_native_invoke(rt, thunk, 0);
        break;
      }
      case F_FORM: {
        if (!datum(f, 0)) return f->bad ? fasl_error(f) : false;
        oti_arena arena;
        oti_arena_init(&arena, OTI_SOURCE_LIMIT);
        oti_ast* form = oti_syntax_value(rt, rt->stack[rt->sp - 1], &arena);
        ot_value result;
        ok = form && oti_evaluate_form(rt, form, &result);
        oti_arena_destroy(&arena);
        break;
      }
      case F_MACRO: {
        if (!datum(f, 0) || !datum(f, 0)) return f->bad ? fasl_error(f) : false;
        uint32_t cell = current_global(rt, rt->stack[base]);
        ok = cell != UINT32_MAX;
        ot_value macro = ok ? oti_alloc(rt, K_MACRO, 1, 0) : 0;
        if (ok && !macro) ok = false;
        if (ok) {
          oti_as_macro(macro)->expander = oti_global_value(rt, cell);
          oti_set_global(rt, cell, macro);
          rt->global_info[cell].macro = 1;
          ok = document_global(rt, cell, rt->stack[base + 1]);
        }
        break;
      }
      case F_DOC: {
        if (!datum(f, 0) || !datum(f, 0) || !datum(f, 0)) return f->bad ? fasl_error(f) : false;
        uint32_t cell = current_global(rt, rt->stack[base]);
        ok = cell != UINT32_MAX;
        if (ok) {
          rt->global_info[cell].is_private = rt->stack[base + 1] == OT_TRUE;
          ok = document_global(rt, cell, rt->stack[base + 2]);
        }
        break;
      }
      case F_DEFONCE: {
        if (!datum(f, 0)) return f->bad ? fasl_error(f) : false;
        uint32_t cell = current_global(rt, rt->stack[base]);
        ok = cell != UINT32_MAX;
        /* The definition and its doc entry are skipped together. */
        if (ok && rt->global_info[cell].defined) {
          rt->sp = rt->stack_roots.count = base;
          if (!skip_entry(f)) return fasl_error(f);
          skip = *f->p == F_DOC;
        }
        break;
      }
      case F_NAMESPACE: {
        if (!datum(f, 0)) return f->bad ? fasl_error(f) : false;
        size_t length;
        const char* text = oti_text_bytes(rt->stack[base], &length);
        uint32_t space = oti_namespace(rt, text, length);
        ok = space != UINT32_MAX;
        if (ok) rt->current_namespace = space;
        break;
      }
      case F_SOURCE: {
        if (!datum(f, 0)) return f->bad ? fasl_error(f) : false;
        ot_value path = rt->stack[base];
        if (oti_kind(path, K_STRING)) {
          size_t length;
          const char* text = oti_text_bytes(path, &length);
          char* copy = malloc(length + 1);
          if (!copy) return oti_error(rt, "out of host memory");
          memcpy(copy, text, length);
          copy[length] = 0;
          rt->source = oti_source_id(rt, copy);
          free(copy);
        } else rt->source = OTI_NO_SOURCE;
        break;
      }
      default: return fasl_error(f);
    }
    rt->sp = rt->stack_roots.count = base;
    if (!ok) return false;
  }
}

static bool load(ot_state* rt, const unsigned char* bytes, size_t length, size_t start,
                 bool in_module) {
  oti_fasl f = {.rt = rt, .base = bytes, .p = bytes + start, .end = bytes + length};
  oti_roots_push(&rt->heap, &f.gensym_roots, NULL, 0);
  bool ok = run(&f, in_module);
  oti_roots_pop(&rt->heap, &f.gensym_roots);
  free(f.gensyms);
  free(f.serials);
  return ok;
}

/* Index a boot file's module sections. */
static bool index_modules(ot_state* rt, oti_boot* boot) {
  oti_fasl f = {.rt = rt, .base = boot->bytes, .p = boot->bytes + 8, .end = boot->bytes + boot->length};
  size_t capacity = 0;
  while (f.p < f.end && *f.p != F_END) {
    if (*f.p != F_MODULE) {
      if (!skip_entry(&f)) return fasl_error(&f);
      continue;
    }
    const unsigned char* at = f.p;
    f.p++;
    if (f.p >= f.end || *f.p != D_SYMBOL) return fasl_error(&f);
    f.p++;
    size_t length;
    const char* name = f_text(&f, &length);
    if (f.bad) return fasl_error(&f);
    size_t body = (size_t)(f.p - boot->bytes);
    f.p = at;
    if (!skip_entry(&f)) return fasl_error(&f);
    if (!oti_grow(rt, (void**)&boot->modules, &capacity, boot->module_count + 1,
                  sizeof(oti_boot_section)))
      return false;
    char* copy = malloc(length + 1);
    if (!copy) return oti_error(rt, "out of host memory");
    memcpy(copy, name, length);
    copy[length] = 0;
    boot->modules[boot->module_count++] = (oti_boot_section){copy, body, (size_t)(f.p - boot->bytes)};
  }
  return true;
}

static unsigned char* read_file(const char* path, size_t* length) {
  FILE* file = fopen(path, "rb");
  if (!file) return NULL;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  unsigned char* bytes = size > 0 ? malloc((size_t)size) : NULL;
  if (bytes && fread(bytes, 1, (size_t)size, file) != (size_t)size) {
    free(bytes);
    bytes = NULL;
  }
  fclose(file);
  *length = bytes ? (size_t)size : 0;
  return bytes;
}

/* Run a fasl's entries once. */
bool oti_load_fasl(ot_state* rt, const char* path) {
  size_t length;
  unsigned char* bytes = read_file(path, &length);
  if (!bytes || length < 8 || memcmp(bytes, "OTFASL01", 8)) {
    free(bytes);
    return oti_errorf(rt, "not a fasl: %s", path);
  }
  uint32_t space = rt->current_namespace, source = rt->source;
  bool ok = load(rt, bytes, length, 8, false);
  rt->current_namespace = space;
  rt->source = source;
  free(bytes);
  return ok;
}

bool oti_load_boot(ot_state* rt, const char* path) {
  FILE* file = fopen(path, "rb");
  if (!file) return oti_errorf(rt, "cannot open boot file %s", path);
  oti_boot* boot = calloc(1, sizeof(*boot));
  if (!boot) {
    fclose(file);
    return oti_error(rt, "out of host memory");
  }
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  boot->bytes = size > 0 ? malloc((size_t)size) : NULL;
  bool ok = boot->bytes && fread(boot->bytes, 1, (size_t)size, file) == (size_t)size;
  fclose(file);
  boot->length = ok ? (size_t)size : 0;
  if (!ok || boot->length < 8 || memcmp(boot->bytes, "OTFASL01", 8)) {
    free(boot->bytes);
    free(boot);
    return oti_errorf(rt, "not a boot file: %s", path);
  }
  rt->boot = boot;
  if (!index_modules(rt, boot)) return false;
  uint32_t space = rt->current_namespace, source = rt->source;
  ok = load(rt, boot->bytes, boot->length, 8, false);
  rt->current_namespace = space;
  rt->source = source;
  return ok;
}

/* Load a module from the boot file, if it has one: *found says whether it
 * did. The caller has set the namespace and module state as for source. */
bool oti_boot_module(ot_state* rt, const char* name, bool* found) {
  *found = false;
  oti_boot* boot = rt->boot;
  if (!boot) return true;
  for (size_t i = 0; i < boot->module_count; i++)
    if (!strcmp(boot->modules[i].name, name)) {
      *found = true;
      return load(rt, boot->bytes, boot->modules[i].end, boot->modules[i].start, true);
    }
  return true;
}

void oti_boot_destroy(ot_state* rt) {
  oti_boot* boot = rt->boot;
  if (!boot) return;
  for (size_t i = 0; i < boot->module_count; i++) free(boot->modules[i].name);
  free(boot->modules);
  free(boot->bytes);
  free(boot);
  rt->boot = NULL;
}

#endif
