#include "internal.h"
#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef OTIUM_BOOTSTRAP_FILE
#define OTIUM_BOOTSTRAP_FILE "c/src/primitives.ot"
#endif

bool oti_host_entry(ot_state* rt) {
  if (!rt->host_io_active) return true;
  rt->host_io_reentry = true;
  return oti_error(rt, "host I/O callback cannot reenter Otium");
}

static char out_of_memory[] = "out of host memory";

/* Start a record whose message has length bytes, or return NULL if one exists. */
static char* start_error(ot_state* rt, size_t length) {
  if (rt->error.set) return NULL;
  oti_error_record* e = &rt->error;
  *e = (oti_error_record){.set = true, .slots = {OT_NIL, OT_NIL, OT_NIL, OT_NIL}};
  rt->signaled = false;
  e->message = length < SIZE_MAX ? malloc(length + 1) : NULL;
  if (!e->message) {
    e->message = out_of_memory;
    return NULL;
  }
  e->owned = true;
  return e->message;
}

static bool verror(ot_state* rt, const char* prefix, const char* format, va_list arguments) {
  if (rt->error.set) return false;
  va_list copy;
  va_copy(copy, arguments);
  int length = vsnprintf(NULL, 0, format, copy);
  va_end(copy);
  size_t skip = prefix ? strlen(prefix) : 0;
  char* message = start_error(rt, length < 0 ? SIZE_MAX : skip + (size_t)length);
  if (message) {
    if (skip) memcpy(message, prefix, skip);
    vsnprintf(message + skip, (size_t)length + 1, format, arguments);
  }
  return false;
}

bool oti_error(ot_state* rt, const char* message) { return oti_errorf(rt, "%s", message); }

bool oti_errorf(ot_state* rt, const char* format, ...) {
  va_list arguments;
  va_start(arguments, format);
  verror(rt, NULL, format, arguments);
  va_end(arguments);
  return false;
}

bool oti_ffi_errorf(ot_state* rt, const char* reason, const char* format, ...) {
  if (rt->error.set) return false;
  char prefix[48];
  snprintf(prefix, sizeof(prefix), "FFI %s: ", reason);
  va_list arguments;
  va_start(arguments, format);
  verror(rt, prefix, format, arguments);
  va_end(arguments);
  rt->error.ffi_reason = reason;
  return false;
}

void oti_clear_error(ot_state* rt) {
  if (rt->error.owned) free(rt->error.message);
  rt->error = (oti_error_record){.slots = {OT_NIL, OT_NIL, OT_NIL, OT_NIL}};
}

void oti_take_error(ot_state* rt, oti_error_record* saved, oti_roots* roots) {
  *saved = rt->error;
  rt->error = (oti_error_record){.slots = {OT_NIL, OT_NIL, OT_NIL, OT_NIL}};
  oti_roots_push(&rt->heap, roots, saved->slots, 4);
}

void oti_restore_error(ot_state* rt, oti_error_record* saved, oti_roots* roots) {
  oti_roots_pop(&rt->heap, roots);
  oti_clear_error(rt);
  rt->error = *saved;
}

void oti_drop_error(ot_state* rt, oti_error_record* saved, oti_roots* roots) {
  oti_roots_pop(&rt->heap, roots);
  if (saved->owned) free(saved->message);
  *saved = (oti_error_record){.slots = {OT_NIL, OT_NIL, OT_NIL, OT_NIL}};
}

bool oti_type_error(ot_state* rt, const char* who, const char* what, ot_value value) {
  if (oti_failed(rt)) return false;
  char* text = NULL;
  size_t length = 0;
  FILE* stream = open_memstream(&text, &length);
  if (stream) {
    ot_write(rt, stream, value);
    if (fclose(stream)) {
      free(text);
      text = NULL;
    }
  }
  oti_coded_errorf(rt, "E0401", "`%s` expected %s, got %s", who, what, text ? text : "a value");
  free(text);
  rt->error.who = who;
  rt->error.slots[OTI_ERROR_VALUE] = value;
  rt->error.has_value = true;
  return false;
}

bool oti_tagged_error(ot_state* rt, const char* who, const char* tag, const char* message,
                      size_t count, const ot_value* values) {
  if (oti_failed(rt)) return false;
  oti_error(rt, message);
  oti_error_record* e = &rt->error;
  e->who = who;
  e->tag = tag;
  e->data_count = count < 3 ? count : 3;
  for (size_t i = 0; i < e->data_count; i++) e->slots[i] = values[i];
  return false;
}

ot_value oti_fail(ot_state* rt, const char* message) {
  oti_error(rt, message);
  return 0;
}

ot_value oti_cons(ot_state* rt, ot_value car, ot_value cdr) {
  ot_value live[] = {car, cdr};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  ot_value result = oti_alloc(rt, K_PAIR, 2, 0);
  if (result) {
    oti_as_pair(result)->car = live[0];
    oti_as_pair(result)->cdr = live[1];
  }
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

bool oti_coded_error(ot_state* rt, const char* code, const char* message) {
  return oti_coded_errorf(rt, code, "%s", message);
}

bool oti_coded_errorf(ot_state* rt, const char* code, const char* format, ...) {
  bool fresh = !rt->error.set;
  va_list arguments;
  va_start(arguments, format);
  verror(rt, NULL, format, arguments);
  va_end(arguments);
  if (fresh && code) snprintf(rt->error.code, sizeof(rt->error.code), "%s", code);
  return false;
}

bool oti_grow(ot_state* rt, void** data, size_t* capacity, size_t needed, size_t width) {
  if (needed <= *capacity) return true;
  size_t next = *capacity ? *capacity : 16;
  while (next < needed) {
    if (next > SIZE_MAX / 2) return oti_error(rt, "allocation size overflow");
    next *= 2;
  }
  if (next > SIZE_MAX / width) return oti_error(rt, "allocation size overflow");
  void* new_data = realloc(*data, next * width);
  if (!new_data) return oti_error(rt, "out of host memory");
  *data = new_data;
  *capacity = next;
  return true;
}

bool oti_stack(ot_state* rt, size_t needed) {
  if (needed > rt->stack_limit) return oti_error(rt, "VM stack limit exceeded");
#ifdef OTIUM_CHECK_ROOTS
  /* Root-checking builds move the stack on every request, sized exactly, so a
   * pointer into it held across possible growth reads freed, poisoned memory. */
  size_t capacity = needed > rt->stack_capacity ? needed : rt->stack_capacity;
  ot_value* moved = malloc((capacity ? capacity : 1) * sizeof(ot_value));
  if (!moved) return oti_error(rt, "out of host memory");
  if (rt->stack_capacity) {
    memcpy(moved, rt->stack, rt->stack_capacity * sizeof(ot_value));
    memset(rt->stack, 0xdb, rt->stack_capacity * sizeof(ot_value));
  }
  free(rt->stack);
  rt->stack = moved;
  rt->stack_capacity = capacity;
#else
  if (!oti_grow(rt, (void**)&rt->stack, &rt->stack_capacity, needed, sizeof(ot_value)))
    return false;
#endif
  rt->stack_roots.slots = rt->stack;
  rt->stack_room = rt->stack_capacity < rt->stack_limit ? rt->stack_capacity : rt->stack_limit;
  return true;
}

void ot_set_interrupt_flag(ot_state* rt, volatile sig_atomic_t* flag) { rt->interrupt_flag = flag; }

bool ot_interrupted(const ot_state* rt) { return rt->interrupted; }

bool ot_poll_interrupt(ot_state* rt) {
  if (!rt->interrupt_flag || !*rt->interrupt_flag) return true;
  *rt->interrupt_flag = 0;
  rt->interrupted = true;
  rt->exit_pending = rt->restart_pending = rt->raised = false;
  rt->condition_values[0] = OT_NIL;
  rt->restart_transfer[0] = rt->restart_transfer[1] = OT_NIL;
  oti_clear_error(rt);
  oti_error(rt, "interrupted");
  return false;
}

ot_value oti_alloc(ot_state* rt, uint32_t kind, uint32_t fields, size_t bytes) {
  if (!oti_host_entry(rt)) return 0;
  if (rt->interrupt_flag && *rt->interrupt_flag && !ot_poll_interrupt(rt)) return 0;
  size_t collections = rt->heap.collections;
  if (rt->stress_gc) oti_heap_collect(&rt->heap);
  ot_value v = oti_heap_alloc(&rt->heap, kind, fields, bytes);
  if (rt->heap.collections != collections) {
    oti_foreign_collect(rt);
    if (rt->stack_stale > rt->sp) rt->stack_stale = rt->sp;
  }
  if (!v) oti_error(rt, "managed heap exhausted");
  return v;
}

uint32_t oti_constant(ot_state* rt, ot_value value) {
  for (size_t i = 0; i < rt->constant_count; i++)
    if (rt->constant_state[i] && rt->constants[i] == value) return (uint32_t)i;
  size_t index = rt->constant_free;
  while (index < rt->constant_count && rt->constant_state[index]) index++;
  if (index >= UINT32_MAX ||
      !oti_grow(rt, (void**)&rt->constants, &rt->constant_capacity, index + 1, sizeof(ot_value)))
    return UINT32_MAX;
  rt->constant_roots.slots = rt->constants;
  if (!oti_grow(rt, (void**)&rt->constant_state, &rt->constant_state_capacity, index + 1, 1))
    return UINT32_MAX;
  if (index == rt->constant_count) rt->constant_count++;
  rt->constant_state[index] = 1;
  rt->constants[index] = value;
  rt->constant_free = index + 1;
  rt->constant_roots.count = rt->constant_count;
  return (uint32_t)index;
}

uint32_t oti_namespace(ot_state* rt, const char* name, size_t length) {
  if (length == 3 && !memcmp(name, "ffi", 3)) {
    name = "otium.ffi";
    length = 9;
  }
  for (size_t i = 0; i < rt->namespace_count; i++)
    if (strlen(rt->namespaces[i]) == length && !memcmp(rt->namespaces[i], name, length))
      return (uint32_t)i;
  if (rt->namespace_count >= UINT32_MAX ||
      !oti_grow(rt, (void**)&rt->namespaces, &rt->namespace_capacity, rt->namespace_count + 1,
                sizeof(char*)))
    return UINT32_MAX;
  char* copy = malloc(length + 1);
  if (!copy) {
    oti_error(rt, "out of host memory");
    return UINT32_MAX;
  }
  memcpy(copy, name, length);
  copy[length] = 0;
  uint32_t id = (uint32_t)rt->namespace_count++;
  rt->namespaces[id] = copy;
  return id;
}

static uint32_t find_global(ot_state* rt, uint32_t space, const char* name, size_t length) {
  for (size_t i = 0; i < rt->global_count; i++)
    if (rt->global_info[i].space == space && strlen(rt->global_info[i].name) == length &&
        !memcmp(rt->global_info[i].name, name, length))
      return (uint32_t)i;
  return UINT32_MAX;
}

static uint32_t namespace_global(ot_state* rt, uint32_t space, const char* name, size_t length) {
  uint32_t existing = find_global(rt, space, name, length);
  if (existing != UINT32_MAX) return existing;
  if (rt->global_count >= UINT32_MAX || !oti_grow(rt, (void**)&rt->globals, &rt->global_capacity,
                                                  rt->global_count + 1, sizeof(ot_value))) {
    oti_error(rt, "too many globals");
    return UINT32_MAX;
  }
  rt->global_roots.slots = rt->globals;
  if (!oti_grow(rt, (void**)&rt->global_info, &rt->global_info_capacity, rt->global_count + 1,
                sizeof(oti_global_info)))
    return UINT32_MAX;
  char* copy = malloc(length + 1);
  if (!copy) {
    oti_error(rt, "out of host memory");
    return UINT32_MAX;
  }
  memcpy(copy, name, length);
  copy[length] = 0;
  uint32_t index = (uint32_t)rt->global_count++;
  rt->global_info[index] = (oti_global_info){.name = copy, .space = space};
  rt->globals[index] = V_UNBOUND;
  rt->global_roots.count = rt->global_count;
  return index;
}

uint32_t oti_global(ot_state* rt, const char* name, size_t length) {
  if (oti_import_global(rt, name, length) != UINT32_MAX) {
    oti_coded_errorf(rt, "E0207", "definition of `%.*s` would replace an import", (int)length,
                     name);
    return UINT32_MAX;
  }
  return namespace_global(rt, rt->current_namespace, name, length);
}

uint32_t oti_binding_global(ot_state* rt, uint32_t space, const char* name, size_t length,
                            bool create) {
  uint32_t index = find_global(rt, space, name, length);
  if (index != UINT32_MAX) return index;
  index = oti_import_global_in(rt, space, name, length);
  if (index != UINT32_MAX) return index;
  index = find_global(rt, 0, name, length);
  if (index != UINT32_MAX && rt->globals[index] != V_UNBOUND) return index;
  return create ? namespace_global(rt, space, name, length) : UINT32_MAX;
}

static uint32_t resolve_global(ot_state* rt, const char* name, size_t length, bool create) {
  uint32_t local = find_global(rt, rt->current_namespace, name, length);
  if (local != UINT32_MAX) return local;
  uint32_t imported = oti_import_global(rt, name, length);
  if (imported != UINT32_MAX) return imported;
  /* A local name containing a slash takes precedence over qualification. */
  const char* slash = memchr(name, '/', length);
  if (slash && slash != name && slash + 1 < name + length) {
    size_t prefix = (size_t)(slash - name);
    uint32_t alias = oti_alias_namespace(rt, name, prefix);
    if (alias != UINT32_MAX)
      return oti_binding_global(rt, alias, slash + 1, length - prefix - 1, create);
    const char* space_name = name;
    if (prefix == 3 && !memcmp(name, "ffi", 3)) {
      space_name = "otium.ffi";
      prefix = 9;
    }
    for (size_t i = 0; i < rt->namespace_count; i++)
      if (strlen(rt->namespaces[i]) == prefix && !memcmp(rt->namespaces[i], space_name, prefix))
        return oti_binding_global(rt, (uint32_t)i, slash + 1, length - (size_t)(slash - name) - 1,
                                  create);
  }
  uint32_t core = find_global(rt, 0, name, length);
  if (core != UINT32_MAX && rt->globals[core] != V_UNBOUND) return core;
  return create ? oti_global(rt, name, length) : UINT32_MAX;
}

uint32_t oti_lookup_global(ot_state* rt, const char* name, size_t length) {
  return resolve_global(rt, name, length, false);
}

uint32_t oti_resolve_global(ot_state* rt, const char* name, size_t length) {
  return resolve_global(rt, name, length, true);
}

ot_value oti_text(ot_state* rt, uint32_t kind, const char* text, size_t length) {
  if (length > SIZE_MAX - sizeof(size_t) - 1 - sizeof(uint64_t)) {
    oti_error(rt, "text too large");
    return 0;
  }
  ot_value v = oti_alloc(rt, kind, 0,
                         sizeof(size_t) + length + 1 + (kind == K_SYMBOL ? sizeof(uint64_t) : 0));
  if (v) {
    unsigned char* bytes = object_bytes(v);
    memcpy(bytes, &length, sizeof(length));
    memcpy(bytes + sizeof(length), text, length);
  }
  return v;
}

ot_value oti_float(ot_state* rt, double number) {
  ot_value v = oti_alloc(rt, K_FLOAT, 0, sizeof(double));
  if (v) memcpy(object_bytes(v), &number, sizeof(number));
  return v;
}

ot_state* ot_create(size_t bytes, size_t stack_limit) {
  ot_state* rt = calloc(1, sizeof(*rt));
  if (!rt) return NULL;
  if (!oti_heap_init(&rt->heap, bytes)) {
    free(rt);
    return NULL;
  }
  oti_roots_push(&rt->heap, &rt->open_port_roots, NULL, 0);
  for (size_t i = 0; i < 3; i++) rt->standard_ports[i] = OT_NIL;
  oti_roots_push(&rt->heap, &rt->standard_port_roots, rt->standard_ports, 3);
  rt->process_values[0] = OT_EMPTY;
  rt->process_values[1] = OT_NIL;
  oti_roots_push(&rt->heap, &rt->process_roots, rt->process_values, 2);
  rt->stack_limit = stack_limit;
  const char* stress = getenv("OTIUM_GC_STRESS");
  rt->stress_gc = stress && *stress && strcmp(stress, "0");
  rt->required_namespace = UINT32_MAX;
  oti_roots_push(&rt->heap, &rt->constant_roots, NULL, 0);
  oti_roots_push(&rt->heap, &rt->global_roots, NULL, 0);
  oti_roots_push(&rt->heap, &rt->stack_roots, NULL, 0);
  oti_roots_push(&rt->heap, &rt->param_roots, NULL, 0);
  oti_roots_push(&rt->heap, &rt->handler_roots, NULL, 0);
  oti_roots_push(&rt->heap, &rt->cleanup_roots, NULL, 0);
  oti_roots_push(&rt->heap, &rt->restart_roots, NULL, 0);
  rt->restart_transfer[0] = rt->restart_transfer[1] = OT_NIL;
  oti_roots_push(&rt->heap, &rt->restart_transfer_roots, rt->restart_transfer, 2);
  rt->condition_values[0] = rt->condition_values[1] = OT_NIL;
  oti_roots_push(&rt->heap, &rt->condition_roots, rt->condition_values, 2);
  oti_clear_error(rt);
  oti_roots_push(&rt->heap, &rt->error_roots, rt->error.slots, 4);
  for (size_t i = 0; i < C_COUNT; i++) rt->classes[i] = OT_NIL;
  oti_roots_push(&rt->heap, &rt->class_roots, rt->classes, C_COUNT);
  if (oti_namespace(rt, "otium.core", 10) == UINT32_MAX || !oti_builtins(rt) ||
      !oti_foreign_init(rt)) {
    ot_destroy(rt);
    return NULL;
  }
  rt->current_namespace = oti_namespace(rt, "user", 4);
  if (rt->current_namespace == UINT32_MAX) {
    ot_destroy(rt);
    return NULL;
  }
  rt->permanent_constants = rt->constant_count;
  return rt;
}

bool ot_set_heap_limit(ot_state* rt, size_t bytes) {
  if (!oti_host_entry(rt)) return false;
  return oti_heap_set_limit(&rt->heap, bytes) ||
         oti_error(rt, "heap limit is below capacity or exceeds addressable size");
}

void ot_destroy(ot_state* rt) {
  if (!rt || !oti_host_entry(rt)) return;
  oti_ports_destroy(rt);
  oti_clear_error(rt);
  oti_foreign_destroy(rt);
  oti_modules_destroy(rt);
  for (size_t i = 0; i < rt->function_count; i++) {
    oti_function* f = rt->functions[i];
    oti_free_function(f);
  }
  for (size_t i = 0; i < rt->global_count; i++) {
    free(rt->global_info[i].name);
    free(rt->global_info[i].doc.text);
  }
  free(rt->global_info);
  for (size_t i = 0; i < rt->namespace_count; i++) free(rt->namespaces[i]);
  free(rt->namespaces);
  free(rt->functions);
  free(rt->constants);
  free(rt->globals);
  free(rt->open_ports);
  free(rt->cleanups);
  free(rt->cleanup_states);
  free(rt->restarts);
  free(rt->restart_scopes);
  free(rt->tries);
  free(rt->params);
  free(rt->handlers);
  free(rt->handler_previous);
  free(rt->frames);
  free(rt->stack);
  free(rt->constant_state);
  oti_heap_destroy(&rt->heap);
  free(rt);
}

static bool evaluate_top(ot_state* rt, const oti_ast* form, ot_value* result);

static bool document(ot_state* rt, uint32_t index, const oti_ast* doc) {
  char* text = NULL;
  size_t length = doc ? doc->as.text.size : 0;
  if (doc) {
    text = malloc(length + 1);
    if (!text) return oti_error(rt, "out of host memory");
    memcpy(text, doc->as.text.data, length);
    text[length] = 0;
  }
  free(rt->global_info[index].doc.text);
  rt->global_info[index].doc = (oti_documentation){text, length};
  return true;
}

/* Top-level begin forms are evaluated in order, so declarations from earlier
 * forms are visible before later forms are compiled. */
static bool evaluate_raw(ot_state* rt, const oti_ast* form, ot_value* result) {
  if (form->kind == AST_LIST && !form->as.list.tail &&
      oti_ast_symbol(form->as.list.first, "begin") && form->as.list.first->next) {
    size_t base = rt->sp;
    for (const oti_ast* child = form->as.list.first->next; child; child = child->next) {
      rt->sp = rt->stack_roots.count = base;
      if (!evaluate_top(rt, child, result)) return false;
    }
    return true;
  }
  if (form->kind == AST_LIST && oti_ast_symbol(form->as.list.first, "defmacro")) {
    const oti_ast* name = form->as.list.first->next;
    if (form->as.list.tail || !name || name->kind != AST_SYMBOL || !name->next)
      return oti_error(rt, "defmacro needs a name and parameters");
    oti_ast definition = *form, head = *form->as.list.first, binding = *name;
    oti_ast lambda = {.kind = AST_LIST, .offset = form->offset};
    oti_ast fn = {.kind = AST_SYMBOL, .as.text = {"fn", 2}, .next = name->next};
    head.as.text.data = "def";
    head.as.text.size = 3;
    head.next = &binding;
    binding.next = &lambda;
    lambda.as.list.first = &fn;
    definition.as.list.first = &head;
    if (!evaluate_raw(rt, &definition, result)) return false;
    uint32_t global = oti_global(rt, name->as.text.data, name->as.text.size);
    if (global == UINT32_MAX) return false;
    ot_value macro = oti_alloc(rt, K_MACRO, 1, 0);
    if (!macro) return false;
    oti_as_macro(macro)->expander = rt->globals[global];
    rt->globals[global] = macro;
    rt->global_info[global].macro = 1;
    const oti_ast* doc = name->next->next;
    if (!document(rt, global, doc && doc->kind == AST_STRING && doc->next ? doc : NULL))
      return false;
    *result = OT_NIL;
    return true;
  }
  bool ok;
  uint32_t function;
  if (form->kind == AST_LIST && oti_ast_symbol(form->as.list.first, "require")) {
    if (form->as.list.tail) return oti_error(rt, "require needs a proper specification list");
    if (!oti_require(rt, form->as.list.first->next)) return false;
    *result = OT_NIL;
    return true;
  }
  if (form->kind == AST_LIST && oti_ast_symbol(form->as.list.first, "ns")) {
    const oti_ast* name = form->as.list.first->next;
    if (form->as.list.tail || !name || name->kind != AST_SYMBOL) {
      rt->error.offset = form->offset;
      return oti_coded_error(rt, "E0110", "ns needs a namespace symbol");
    }
    if (rt->required_namespace != UINT32_MAX &&
        (strlen(rt->namespaces[rt->required_namespace]) != name->as.text.size ||
         memcmp(rt->namespaces[rt->required_namespace], name->as.text.data, name->as.text.size)))
      return oti_coded_error(rt, "E0206", "module namespace does not match required name");
    uint32_t space = oti_namespace(rt, name->as.text.data, name->as.text.size);
    if (space == UINT32_MAX) return false;
    rt->current_namespace = space;
    for (const oti_ast* clause = name->next; clause; clause = clause->next) {
      const oti_ast* head = clause->kind == AST_LIST ? clause->as.list.first : NULL;
      if (!head || clause->as.list.tail || head->kind != AST_KEYWORD || head->as.text.size != 8 ||
          memcmp(head->as.text.data, ":require", 8))
        return oti_coded_error(rt, "E0110", "ns only accepts require clauses");
      if (!oti_require(rt, head->next)) return false;
    }
    *result = OT_NIL;
    return true;
  }
  oti_ast replacement, head;
  bool once = form->kind == AST_LIST && oti_ast_symbol(form->as.list.first, "defonce");
  if (once) {
    const oti_ast* name = form->as.list.first->next;
    const oti_ast* value = name ? name->next : NULL;
    if (form->as.list.tail || !name || name->kind != AST_SYMBOL || !value ||
        (value->next && value->next->next)) {
      rt->error.offset = form->offset;
      return oti_error(rt, "defonce needs a name, optional docstring and value");
    }
    uint32_t global = oti_global(rt, name->as.text.data, name->as.text.size);
    if (global == UINT32_MAX) return false;
    if (rt->global_info[global].defined) {
      *result = OT_NIL;
      return true;
    }
    replacement = *form;
    head = *form->as.list.first;
    head.as.text.data = "def";
    head.as.text.size = 3;
    replacement.as.list.first = &head;
    form = &replacement;
  }
  bool compiling = rt->compiling;
  rt->compiling = true;
  ok = oti_compile(rt, form, &function);
  rt->compiling = compiling;
  /* Reclaim unreachable code at the next idle boundary. Macro execution can
     * retain constants and nested eval closures even if this compile fails. */
  if (!ok || oti_failed(rt)) return false;
  if (!oti_execute(rt, function, result)) return false;
  if (form->kind == AST_LIST) {
    const oti_ast* definition = form->as.list.first;
    if (oti_ast_symbol(definition, "def") || oti_ast_symbol(definition, "def-") ||
        oti_ast_symbol(definition, "defparam")) {
      const oti_ast* name = definition->next;
      if (name->kind == AST_LIST) name = name->as.list.first;
      uint32_t index =
          find_global(rt, rt->current_namespace, name->as.text.data, name->as.text.size);
      if (index != UINT32_MAX) {
        rt->global_info[index].is_private = oti_ast_symbol(definition, "def-");
        const oti_ast* doc = definition->next->next;
        if (!document(rt, index, doc && doc->kind == AST_STRING && doc->next ? doc : NULL))
          return false;
      }
    }
  }
  if (once) *result = OT_NIL;
  return true;
}

static bool evaluate_expanded(ot_state* rt, const oti_ast* form, ot_value* result) {
  if (!rt->shared_expander) return evaluate_raw(rt, form, result);
  const oti_ast* head = form->kind == AST_LIST ? form->as.list.first : NULL;
  if (oti_ast_symbol(head, "begin") || oti_ast_symbol(head, "require") ||
      oti_ast_symbol(head, "ns"))
    return evaluate_raw(rt, form, result);
  if (oti_ast_symbol(head, "defonce") && head->next && head->next->kind == AST_SYMBOL) {
    const oti_ast* name = head->next;
    uint32_t global =
        find_global(rt, rt->current_namespace, name->as.text.data, name->as.text.size);
    if (global != UINT32_MAX && rt->global_info[global].defined)
      return evaluate_raw(rt, form, result);
  }
  oti_arena arena;
  oti_arena_init(&arena, OTI_SOURCE_LIMIT);
  oti_ast* expanded = oti_expand_form(rt, form, &arena);
  bool ok = expanded && evaluate_raw(rt, expanded, result);
  oti_arena_destroy(&arena);
  return ok;
}

static bool evaluate_top(ot_state* rt, const oti_ast* form, ot_value* result) {
  size_t base = rt->frame_count || rt->compiling || rt->evaluating || rt->eval_depth ? rt->sp : 0;
  rt->sp = rt->stack_roots.count = base;
  if (!rt->frame_count && !rt->compiling && !rt->evaluating && !rt->eval_depth && !oti_reclaim(rt))
    return false;
  bool evaluating = rt->evaluating;
  rt->evaluating = true;
  bool ok = evaluate_expanded(rt, form, result);
  rt->evaluating = evaluating;
  rt->sp = rt->stack_roots.count = base;
  if (!ok || !oti_stack(rt, base + 1)) return false;
  rt->stack[base] = *result;
  rt->sp = rt->stack_roots.count = base + 1;
  return true;
}

ot_value oti_eval_value(ot_state* rt, ot_value value) {
  if (rt->eval_depth >= 128) {
    oti_error(rt, "eval nesting limit exceeded");
    return 0;
  }
  rt->eval_depth++;
  oti_arena arena;
  oti_arena_init(&arena, OTI_SOURCE_LIMIT);
  oti_ast* form = oti_syntax_value(rt, value, &arena);
  size_t base = rt->sp, frames = rt->frame_count, tries = rt->try_count;
  size_t restarts = rt->restart_scope_count, cleanups = rt->cleanup_count;
  ot_value result = 0;
  if (form && !evaluate_top(rt, form, &result)) result = 0;
  rt->sp = rt->stack_roots.count = base;
  rt->frame_count = frames;
  rt->try_count = tries;
  oti_restore_restarts(rt, restarts);
  oti_restore_cleanups(rt, cleanups);
  oti_arena_destroy(&arena);
  rt->eval_depth--;
  return result;
}

#ifndef OTIUM_FREESTANDING
static int source_open(ot_state* rt, const char* path, intptr_t* handle) {
  (void)rt;
  FILE* file = fopen(path, "rb");
  *handle = (intptr_t)file;
  return file ? 0 : errno ? errno : EIO;
}

static long source_read(ot_state* rt, intptr_t handle, char* bytes, size_t length) {
  (void)rt;
  size_t got = fread(bytes, 1, length, (FILE*)handle);
  return ferror((FILE*)handle) ? -EIO : (long)got;
}

static void source_close(ot_state* rt, intptr_t handle) {
  (void)rt;
  fclose((FILE*)handle);
}
#else
/* Without an operating system, source files come from the I/O host. */
static int source_open(ot_state* rt, const char* path, intptr_t* handle) {
  if (!rt->io_host_set) return ENOSYS;
  rt->host_io_active = true;
  int error = rt->io_host.open(rt->io_userdata, path, OT_IO_READ, handle);
  rt->host_io_active = rt->host_io_reentry = false;
  return error;
}

static long source_read(ot_state* rt, intptr_t handle, char* bytes, size_t length) {
  rt->host_io_active = true;
  long n = rt->io_host.read(rt->io_userdata, handle, (uint8_t*)bytes, length);
  rt->host_io_active = rt->host_io_reentry = false;
  return n > (long)length ? -EIO : n;
}

static void source_close(ot_state* rt, intptr_t handle) {
  rt->host_io_active = true;
  rt->io_host.close(rt->io_userdata, handle);
  rt->host_io_active = rt->host_io_reentry = false;
}
#endif

char* oti_read_source_file(ot_state* rt, const char* path, size_t limit, const char* what,
                           size_t* length, int* open_error) {
  intptr_t handle;
  *open_error = source_open(rt, path, &handle);
  if (*open_error) return NULL;
  char* buffer = NULL;
  size_t used = 0, capacity = 0;
  bool ok = true;
  for (;;) {
    if (used == capacity) {
      size_t next = capacity ? capacity * 2 : 4096;
      if (next > limit + 1) next = limit + 1;
      char* grown = realloc(buffer, next + 1);
      if (!grown) {
        ok = oti_error(rt, "out of host memory");
        break;
      }
      buffer = grown;
      capacity = next;
    }
    long n = source_read(rt, handle, buffer + used, capacity - used);
    if (n < 0) {
      ok = oti_errorf(rt, "cannot read %s", what);
      break;
    }
    used += (size_t)n;
    if (used > limit) {
      ok = oti_errorf(rt, "%s exceeds the source size limit", what);
      break;
    }
    if (!n) break;
  }
  source_close(rt, handle);
  if (!ok) {
    free(buffer);
    return NULL;
  }
  buffer[used] = 0;
  *length = used;
  return buffer;
}

/* Evaluate a runtime library file in the root namespace. */
static bool eval_library_file(ot_state* rt, const char* path, const char* what) {
  size_t length;
  int error;
  char* source = oti_read_source_file(rt, path, OTI_SOURCE_LIMIT, what, &length, &error);
  if (error) return oti_errorf(rt, "cannot open %s", what);
  if (!source) return false;
  uint32_t saved = rt->current_namespace;
  rt->current_namespace = 0;
  ot_value result;
  bool ok = ot_eval(rt, source, length, &result);
  rt->current_namespace = saved;
  free(source);
  return ok;
}

/* Join a library directory and a relative path. NULL when out of memory. */
static char* library_path(ot_state* rt, const char* directory, const char* file) {
  size_t n = strlen(directory) + strlen(file) + 2;
  char* path = malloc(n);
  if (!path) {
    oti_error(rt, "out of host memory");
    return NULL;
  }
  snprintf(path, n, "%s/%s", directory, file);
  return path;
}

bool ot_load_expander(ot_state* rt, const char* directory) {
  if (!oti_host_entry(rt)) return false;
  if (rt->shared_expander) return true;
  if (!oti_classes_init(rt)) return false;
  char* path = library_path(rt, directory, "otium/expander.ot");
  if (!path) return false;
  bool ok = eval_library_file(rt, path, "shared expander");
  free(path);
  if (ok) {
    const char* adapters =
        "(def (macroexpand-1 form) (expander-expand-head form #t)) "
        "(def (macroexpand form) (expander-expand-head form #f)) "
        "(def (macroexpand-all form) (*expander* form)) "
        "(def (type-pred type) (fn (condition) (condition-of-type? condition type)))";
    uint32_t saved = rt->current_namespace;
    rt->current_namespace = 0;
    ot_value result;
    ok = ot_eval(rt, adapters, strlen(adapters), &result);
    rt->current_namespace = saved;
  }
  if (ok) rt->shared_expander = true;
  return ok;
}

bool ot_load_core(ot_state* rt, const char* directory) {
  if (!oti_host_entry(rt)) return false;
  if (rt->core_loaded) return true;
  if (!ot_load_expander(rt, directory) || !ot_add_load_path(rt, directory)) return false;
  char* path = library_path(rt, directory, "otium/core.ot");
  if (!path) return false;
  bool ok = eval_library_file(rt, OTIUM_BOOTSTRAP_FILE, "bootstrap library") &&
            eval_library_file(rt, path, "core library");
  free(path);
  if (ok) rt->core_loaded = true;
  return ok;
}

bool ot_eval(ot_state* rt, const char* source, size_t length, ot_value* result) {
  if (!oti_host_entry(rt)) return false;
  bool nested = rt->frame_count || rt->compiling || rt->evaluating || rt->eval_depth;
  size_t base = nested ? rt->sp : 0, frames = rt->frame_count, tries = rt->try_count;
  size_t restarts = nested ? rt->restart_scope_count : 0, cleanups = nested ? rt->cleanup_count : 0;
  if (!nested) {
    rt->interrupted = false;
    rt->exit_pending = false;
    oti_clear_error(rt);
    rt->raised = false;
    rt->condition_values[0] = OT_NIL;
    rt->try_count = 0;
    oti_restore_cleanups(rt, 0);
    oti_restore_restarts(rt, 0);
    rt->restart_pending = false;
    rt->restart_transfer[0] = rt->restart_transfer[1] = OT_NIL;
    oti_restore_params(rt, 0);
    oti_restore_handlers(rt, 0, 0);
    rt->signaled = false;
  }
  rt->sp = rt->stack_roots.count = base;
  if (!ot_poll_interrupt(rt)) return false;
  oti_reader reader;
  oti_reader_init(&reader, source, length, OTI_SOURCE_LIMIT);
  bool ok = true;
  *result = OT_NIL;
  oti_ast* form;
  while ((form = oti_reader_next(&reader))) {
    rt->sp = rt->stack_roots.count = base;
    if (!(ok = evaluate_top(rt, form, result))) break;
  }
  if (reader.error) {
    oti_coded_error(rt, reader.error_code, reader.error);
    rt->error.offset = reader.error_offset;
  }
  oti_reader_destroy(&reader);
  rt->frame_count = frames;
  rt->try_count = tries;
  oti_restore_restarts(rt, restarts);
  oti_restore_cleanups(rt, cleanups);
  rt->sp = rt->stack_roots.count = base;
  if (!ok || oti_failed(rt)) return false;
  if (!oti_stack(rt, base + 1)) return false;
  rt->stack[base] = *result;
  rt->sp = rt->stack_roots.count = base + 1;
  return true;
}

bool ot_exit_status(const ot_state* rt, int* status) {
  if (!rt->exit_pending) return false;
  *status = rt->exit_status;
  return true;
}

const char* ot_error(const ot_state* rt) { return rt->error.set ? rt->error.message : ""; }

bool ot_call(ot_state* rt, const char* name, const ot_value* args, size_t count, ot_value* result) {
  if (!oti_host_entry(rt)) return false;
  rt->interrupted = false;
  rt->exit_pending = false;
  oti_clear_error(rt);
  rt->raised = false;
  rt->condition_values[0] = OT_NIL;
  rt->try_count = 0;
  oti_restore_cleanups(rt, 0);
  oti_restore_restarts(rt, 0);
  rt->restart_pending = false;
  rt->restart_transfer[0] = rt->restart_transfer[1] = OT_NIL;
  oti_restore_params(rt, 0);
  oti_restore_handlers(rt, 0, 0);
  rt->signaled = false;
  rt->sp = rt->stack_roots.count = rt->frame_count = 0;
  if (!ot_poll_interrupt(rt)) return false;
  uint32_t index = resolve_global(rt, name, strlen(name), false);
  if (index == UINT32_MAX || rt->globals[index] == V_UNBOUND)
    return oti_error(rt, "unbound global");
  if (count >= rt->stack_limit || !oti_stack(rt, count + 1))
    return oti_error(rt, "VM stack limit exceeded");
  rt->stack[0] = rt->globals[index];
  if (count) memcpy(rt->stack + 1, args, count * sizeof(ot_value));
  rt->sp = rt->stack_roots.count = count + 1;
  if (!oti_invoke(rt, count, result)) {
    oti_restore_cleanups(rt, 0);
    oti_restore_restarts(rt, 0);
    rt->restart_pending = false;
    rt->restart_transfer[0] = rt->restart_transfer[1] = OT_NIL;
    oti_restore_params(rt, 0);
    oti_restore_handlers(rt, 0, 0);
    rt->signaled = false;
    rt->sp = rt->stack_roots.count = rt->frame_count = 0;
    return false;
  }
  return true;
}

size_t ot_error_offset(const ot_state* rt) { return rt->error.offset; }

ot_stats ot_get_stats(const ot_state* rt) {
  ot_stats stats = {.collections = rt->heap.collections,
                    .max_frames = rt->max_frames,
                    .heap_capacity = rt->heap.capacity};
  oti_foreign_stats(rt, &stats);
  return stats;
}

const char* ot_string_data(ot_value v, size_t* length) {
  if (!oti_kind(v, K_STRING)) return NULL;
  *length = oti_bytes_length(v);
  return (const char*)oti_bytes_data(v);
}

ot_form_status ot_scan_form(const char* source, size_t length, size_t start, size_t* end,
                            const char** error, size_t* error_offset) {
  oti_reader reader;
  oti_reader_init(&reader, source, length, OTI_SOURCE_LIMIT);
  reader.position = start;
  oti_ast* form = oti_reader_next(&reader);
  ot_form_status status = form                ? OT_FORM_COMPLETE
                          : !reader.error     ? OT_FORM_END
                          : reader.incomplete ? OT_FORM_INCOMPLETE
                                              : OT_FORM_ERROR;
  *end = reader.position;
  *error = reader.error;
  *error_offset = reader.error_offset;
  oti_reader_destroy(&reader);
  return status;
}

void ot_gc_stress(ot_state* rt, bool enabled) { rt->stress_gc = enabled; }

static void print_value(FILE* out, ot_value v, unsigned depth) {
  if (depth > 100) {
    fputs("...", out);
    return;
  }
  if (ot_is_int(v)) {
    fprintf(out, "%" PRIdPTR, ot_to_int(v));
    return;
  }
  if (v == OT_NIL) fputs("nil", out);
  else if (v == OT_FALSE) fputs("#f", out);
  else if (v == OT_TRUE) fputs("#t", out);
  else if (v == OT_EMPTY) fputs("()", out);
  else if (v == OT_EOF) fputs("#<eof>", out);
  else if (oti_kind(v, K_PARAM)) {
    fputs("#<param ", out);
    print_value(out, oti_as_param(v)->name, depth + 1);
    fputc('>', out);
  } else if ((v & 255) == TAG_CHAR) {
    uint32_t cp = (uint32_t)(v >> 8);
    char bytes[4];
    if (!depth) {
      size_t n = oti_utf8_encode(cp, bytes);
      fwrite(bytes, 1, n, out);
    } else if (cp == 0) fputs("#\\nul", out);
    else if (cp == 32) fputs("#\\space", out);
    else if (cp == 10) fputs("#\\newline", out);
    else if (cp == 9) fputs("#\\tab", out);
    else if (cp == 13) fputs("#\\return", out);
    else if (cp < 32 || cp == 127) fprintf(out, "#\\x%x", cp);
    else {
      fputs("#\\", out);
      size_t n = oti_utf8_encode(cp, bytes);
      fwrite(bytes, 1, n, out);
    }
  } else if ((v & 255) == TAG_BUILTIN) fputs("#<builtin>", out);
  else if (oti_kind(v, K_FLOAT)) {
    double d;
    char buffer[64];
    memcpy(&d, object_bytes(v), sizeof(d));
    oti_format_float(d, buffer, sizeof(buffer));
    fputs(buffer, out);
  } else if (oti_kind(v, K_STRING) || oti_kind(v, K_SYMBOL) || oti_kind(v, K_KEYWORD)) {
    size_t n;
    unsigned char* s = object_bytes(v);
    memcpy(&n, s, sizeof(n));
    s += sizeof(n);
    if (oti_kind(v, K_STRING) && depth) {
      fputc('"', out);
      for (size_t i = 0; i < n; i++) {
        switch (s[i]) {
          case '"': fputs("\\\"", out); break;
          case '\\': fputs("\\\\", out); break;
          case '\n': fputs("\\n", out); break;
          case '\r': fputs("\\r", out); break;
          case '\t': fputs("\\t", out); break;
          case 0: fputs("\\0", out); break;
          case 27: fputs("\\e", out); break;
          default:
            if (s[i] < 32 || s[i] == 127) fprintf(out, "\\x%x;", s[i]);
            else fputc(s[i], out);
        }
      }
      fputc('"', out);
    } else fwrite(s, 1, n, out);
  } else if (oti_kind(v, K_RECORD) || oti_kind(v, K_RECORD_TYPE)) {
    bool descriptor = oti_kind(v, K_RECORD_TYPE);
    fputs(descriptor ? "#<record-type " : "#<record ", out);
    print_value(out, object_fields(descriptor ? v : oti_as_record(v)->type)[0], depth + 1);
    fputc('>', out);
  } else if (oti_kind(v, K_INSTANCE)) {
    fputs("#<", out);
    print_value(out, oti_as_class(oti_as_instance(v)->type)->name, depth + 1);
    fputc('>', out);
  } else if (oti_kind(v, K_PORT)) {
    oti_port_repr(out, v);
  } else if (oti_kind(v, K_RESTART)) {
    fputs("#<restart ", out);
    print_value(out, oti_as_restart(v)->name, depth + 1);
    fputc('>', out);
  } else if (oti_kind(v, K_BYTEVECTOR)) {
    fputs("#u8(", out);
    for (size_t i = 0; i < oti_bytes_length(v); i++) {
      if (i) fputc(' ', out);
      fprintf(out, "%u", oti_bytes_data(v)[i]);
    }
    fputc(')', out);
  } else if (oti_kind(v, K_ARRAY)) {
    oti_array_obj* info = oti_as_array(v);
    ot_value* items = info->length ? object_fields(info->data) : NULL;
    fputc('[', out);
    for (size_t i = 0; i < info->length; i++) {
      if (i) fputc(' ', out);
      print_value(out, items[i], depth + 1);
    }
    fputc(']', out);
  } else if (oti_kind(v, K_TABLE)) {
    oti_table_obj* info = oti_as_table(v);
    ot_value* entries = info->used ? object_fields(info->data) : NULL;
    fputc('{', out);
    size_t printed = 0;
    for (size_t i = 0; i < info->used; i++)
      if (entries[2 * i + 1] != OT_NIL) {
        if (printed++) fputc(' ', out);
        print_value(out, entries[2 * i], depth + 1);
        fputc(' ', out);
        print_value(out, entries[2 * i + 1], depth + 1);
      }
    fputc('}', out);
  } else if (oti_kind(v, K_PAIR)) {
    fputc('(', out);
    unsigned count = 0;
    while (oti_kind(v, K_PAIR) && count++ < 1000) {
      if (count > 1) fputc(' ', out);
      print_value(out, oti_car(v), depth + 1);
      v = oti_cdr(v);
    }
    if (v != OT_EMPTY) {
      fputs(" . ", out);
      print_value(out, v, depth + 1);
    }
    fputc(')', out);
  } else if (oti_kind(v, K_CLASS)) fputs("#<descriptor>", out);
  else if (oti_kind(v, K_POINTER)) fputs("#<foreign-pointer>", out);
  else if (oti_kind(v, K_FOREIGN_LAYOUT)) fputs("#<foreign-layout>", out);
  else if (oti_kind(v, K_FOREIGN_LIBRARY)) fputs("#<foreign-library>", out);
  else fputs("#<function>", out);
}

void ot_print(ot_state* rt, FILE* out, ot_value value) {
  (void)rt;
  print_value(out, value, 0);
}

void ot_write(ot_state* rt, FILE* out, ot_value value) {
  (void)rt;
  print_value(out, value, 1);
}
