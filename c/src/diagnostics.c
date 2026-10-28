#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Error locations and reports. Errors record where they happened as source
 * ids and byte offsets, without touching the managed heap; ot_report_error
 * turns them into a diagnostic table and renders it with otium.diagnostics.
 * See the Errors section of scratch/runtime-rewrite.md. */

static char* copy_string(const char* s) {
  if (!s) return NULL;
  size_t n = strlen(s) + 1;
  char* copy = malloc(n);
  if (copy) memcpy(copy, s, n);
  return copy;
}

uint32_t oti_source_id(ot_state* rt, const char* path) {
  for (size_t i = 1; i < rt->source_count; i++)
    if (!strcmp(rt->sources[i], path)) return (uint32_t)i;
  size_t index = rt->source_count ? rt->source_count : 1;
  if (index >= OTI_NO_SOURCE ||
      !oti_grow(rt, (void**)&rt->sources, &rt->source_capacity, index + 1, sizeof(char*)))
    return OTI_NO_SOURCE;
  char* copy = copy_string(path);
  if (!copy) {
    oti_error(rt, "out of host memory");
    return OTI_NO_SOURCE;
  }
  rt->sources[0] = NULL;
  rt->sources[index] = copy;
  rt->source_count = index + 1;
  return (uint32_t)index;
}

void oti_error_at(ot_state* rt, size_t offset) {
  oti_error_record* e = &rt->error;
  if (!e->set || e->located || rt->source == OTI_NO_SOURCE) return;
  e->offset = offset;
  e->source = rt->source;
  e->located = true;
}

/* The offset of the form that compiled to instruction pc. */
static uint32_t point_offset(const oti_function* fn, size_t pc) {
  size_t low = 0, high = fn->point_count;
  while (high - low > 1) {
    size_t middle = low + (high - low) / 2;
    if (fn->points[middle].pc <= pc) low = middle;
    else high = middle;
  }
  return fn->point_count ? fn->points[low].offset : 0;
}

#define TRACE_LIMIT 200

void oti_trace_error(ot_state* rt) {
  oti_error_record* e = &rt->error;
  if (!e->set || e->traced || rt->try_count || rt->interrupted || rt->exit_pending ||
      rt->restart_pending)
    return;
  e->traced = true;
  size_t count = 0;
  for (size_t i = rt->frame_count; i-- && count < TRACE_LIMIT;)
    if (rt->frames[i].fn->source != OTI_NO_SOURCE) count++;
  if (!count || !(e->frames = calloc(count, sizeof(oti_error_place)))) return;
  for (size_t i = rt->frame_count; i-- && e->frame_count < count;) {
    const oti_frame* frame = &rt->frames[i];
    const oti_function* fn = frame->fn;
    if (fn->source == OTI_NO_SOURCE) continue;
    size_t pc = frame->pc > fn->code ? (size_t)(frame->pc - fn->code) - 1 : 0;
    /* A tail call has replaced its caller's frame by the time the callee
     * fails, though a builtin callee runs on the caller's. */
    unsigned op = pc < fn->count ? fn->code[pc].op : OP_COUNT;
    if (i == rt->frame_count - 1 && (op == OP_TAIL_CALL || op == OP_TAIL_CALL_GLOBAL) &&
        strcmp(e->code, "E0201"))
      continue;
    /* Recursion through one call site shows once. */
    uint32_t offset = point_offset(fn, pc);
    const oti_error_place* last = e->frame_count ? &e->frames[e->frame_count - 1] : NULL;
    if (last && last->source == fn->source && last->offset == offset) continue;
    e->frames[e->frame_count++] = (oti_error_place){fn->source, offset, copy_string(fn->name)};
  }
}

void oti_locate_error(ot_state* rt, size_t offset) {
  oti_error_record* e = &rt->error;
  if (!e->set || rt->source == OTI_NO_SOURCE || rt->interrupted || rt->exit_pending) return;
  if (!e->frame_count && !e->located) {
    oti_error_at(rt, offset);
    return;
  }
  uint32_t where = e->frame_count ? e->frames[0].source : e->source;
  if (where == rt->source ||
      (e->reached_count && e->reached[e->reached_count - 1].source == rt->source))
    return;
  oti_error_place* grown = realloc(e->reached, (e->reached_count + 1) * sizeof(*grown));
  if (!grown) return;
  e->reached = grown;
  e->reached[e->reached_count++] = (oti_error_place){rt->source, (uint32_t)offset, NULL};
}

/* "1", "1 or 2", "1, 2, or 3" or "at least 1". */
static void arity_text(const oti_function* fn, char* out, size_t size) {
  if (fn->rest) {
    snprintf(out, size, "at least %zu", fn->arity);
    return;
  }
  size_t used = 0;
  for (size_t n = fn->arity; n <= fn->arity + fn->optional && used < size; n++) {
    const char* separator = n == fn->arity                 ? ""
                            : n < fn->arity + fn->optional ? ", "
                            : fn->optional == 1            ? " or "
                                                           : ", or ";
    used += (size_t)snprintf(out + used, size - used, "%s%zu", separator, n);
  }
}

bool oti_arity_error(ot_state* rt, const oti_function* fn, size_t argc) {
  if (oti_failed(rt)) return false;
  const char* name = fn->name ? fn->name : "function";
  oti_coded_errorf(rt, "E0301", "wrong number of arguments to `%s`", name);
  char expected[96], label[160];
  arity_text(fn, expected, sizeof(expected));
  snprintf(label, sizeof(label), "expects %s argument%s, called with %zu", expected,
           strcmp(expected, "1") ? "s" : "", argc);
  rt->error.name = copy_string(name);
  rt->error.label = copy_string(label);
  return false;
}

/* The printed form of value, owned, or NULL. */
static char* written(ot_state* rt, ot_value value) {
  char* text = NULL;
  size_t length = 0;
  FILE* stream = open_memstream(&text, &length);
  if (!stream) return NULL;
  ot_write(rt, stream, value);
  if (fclose(stream)) {
    free(text);
    return NULL;
  }
  return text;
}

bool oti_callable_error(ot_state* rt, ot_value value) {
  if (oti_failed(rt)) return false;
  char* text = written(rt, value);
  oti_coded_errorf(rt, "E0302", "%s is not callable", text ? text : "value");
  free(text);
  rt->error.slots[OTI_ERROR_VALUE] = value;
  rt->error.has_value = true;
  return false;
}

/* A name from another namespace is shown qualified, as it was written. */
bool oti_unbound_error(ot_state* rt, uint32_t global) {
  if (oti_failed(rt)) return false;
  const oti_global_info* info = &rt->global_info[global];
  const char* prefix = "";
  if (info->space && info->space != rt->current_namespace) {
    prefix = oti_namespace_alias(rt, rt->current_namespace, info->space);
    if (!prefix) prefix = rt->namespaces[info->space];
  }
  const char* slash = *prefix ? "/" : "";
  oti_coded_errorf(rt, "E0201", "unbound name `%s%s%s`", prefix, slash, info->name);
  size_t qualifier = strlen(prefix) + strlen(slash), n = qualifier + strlen(info->name) + 1;
  char* name = malloc(n);
  if (name) snprintf(name, n, "%s%s%s", prefix, slash, info->name);
  rt->error.name = name;
  rt->error.space = info->space;
  rt->error.qualifier = qualifier;
  return false;
}

/* Reports. Values under construction live on the VM stack above sp, which is
 * rooted, and are named by their slot. */

typedef struct {
  uint32_t source;
  char* text;
  size_t length;
} oti_report_text;

typedef struct {
  ot_state* rt;
  const char* host_path;
  oti_report_text texts[8];
  size_t text_count;
} oti_report;

static const char* source_path(oti_report* r, uint32_t source) {
  if (source == 0) return r->host_path ? r->host_path : r->rt->host_path;
  return source < r->rt->source_count ? r->rt->sources[source] : NULL;
}

/* The text of a source file, or NULL when it cannot be read. */
static const char* source_text(oti_report* r, uint32_t source, size_t* length) {
  for (size_t i = 0; i < r->text_count; i++)
    if (r->texts[i].source == source) {
      *length = r->texts[i].length;
      return r->texts[i].text;
    }
  /* An error is pending while reporting, so a failed read records nothing. */
  const char* path = source_path(r, source);
  size_t n = 0;
  int error;
  char* text =
      path ? oti_read_source_file(r->rt, path, OTI_SOURCE_LIMIT, "source file", &n, &error) : NULL;
  if (!text) n = 0;
  if (r->text_count < sizeof(r->texts) / sizeof(r->texts[0]))
    r->texts[r->text_count++] = (oti_report_text){source, text, n};
  *length = n;
  return text;
}

/* Path without . and .. segments, relative to the current directory when it
 * lies beneath it. Owned, or NULL. */
static char* display_path(const char* path) {
  size_t n = strlen(path);
  char* out = malloc(n + 2);
  if (!out) return NULL;
  bool absolute = path[0] == '/';
  size_t used = 0, segments = 0;
  size_t* starts = malloc((n / 2 + 2) * sizeof(size_t));
  if (!starts) {
    free(out);
    return NULL;
  }
  if (absolute) out[used++] = '/';
  for (size_t i = 0; i <= n;) {
    size_t end = i;
    while (end < n && path[end] != '/') end++;
    size_t length = end - i;
    if (!length || (length == 1 && path[i] == '.')) {
    } else if (length == 2 && path[i] == '.' && path[i + 1] == '.' && segments &&
               strncmp(out + starts[segments - 1], "..", 2)) {
      used = starts[--segments];
      if (used > (size_t)absolute) used--;
    } else {
      if (segments) out[used++] = '/';
      starts[segments++] = used;
      memcpy(out + used, path + i, length);
      used += length;
    }
    i = end + 1;
  }
  out[used] = 0;
  free(starts);
#ifdef OTIUM_FREESTANDING
  char* here = NULL;
#else
  char* here = absolute ? getcwd(NULL, 0) : NULL;
#endif
  if (here) {
    size_t h = strlen(here);
    if (h > 1 && used > h + 1 && !strncmp(out, here, h) && out[h] == '/')
      memmove(out, out + h + 1, used - h);
    free(here);
  }
  return out;
}

static bool push(ot_state* rt, ot_value v) {
  size_t n = rt->sp;
  if (!v || !oti_stack(rt, n + 1)) return false;
  rt->stack[n] = v;
  rt->sp = n + 1;
  return true;
}

static void drop_to(ot_state* rt, size_t height) { rt->sp = height; }

static bool push_text(ot_state* rt, const char* text) {
  return push(rt, oti_text(rt, K_STRING, text, strlen(text)));
}

/* Set key of the table in slot to the value on top of the stack, and pop it. */
static bool put(ot_state* rt, size_t table, const char* key) {
  ot_value k = oti_intern(rt, K_KEYWORD, key, strlen(key));
  bool ok = k && oti_table_set(rt, rt->stack[table], k, rt->stack[rt->sp - 1]);
  drop_to(rt, rt->sp - 1);
  return ok;
}

static ot_value field_get(ot_state* rt, size_t table, const char* key) {
  ot_value k = oti_intern(rt, K_KEYWORD, key, strlen(key));
  return k ? oti_table_get(rt, rt->stack[table], k, OT_NIL) : 0;
}

/* Replace the values from slot base up with an array of them. */
static bool push_array(ot_state* rt, size_t base) {
  ot_value array = oti_array_from(rt, base, rt->sp - base);
  drop_to(rt, base);
  return push(rt, array);
}

static size_t characters(const char* text, size_t from, size_t to) {
  size_t n = 0;
  for (size_t i = from; i < to; i++) n += ((unsigned char)text[i] & 0xc0) != 0x80;
  return n;
}

/* Push a span table for bytes [start, end) of source, or nil when the source
 * cannot be read. end 0 means the form that starts at start. */
static bool push_span(oti_report* r, uint32_t source, size_t start, size_t end) {
  ot_state* rt = r->rt;
  size_t length;
  const char* text = source_text(r, source, &length);
  if (!text || start > length) return push(rt, OT_NIL);
  if (!end) {
    const char* message;
    size_t ignored;
    if (ot_scan_form(text, length, start, &end, &message, &ignored) != OT_FORM_COMPLETE)
      end = start + 1;
  }
  if (end > length) end = length;
  size_t line = 1, line_start = 0;
  for (size_t i = 0; i < start; i++)
    if (text[i] == '\n') {
      line++;
      line_start = i + 1;
    }
  char* path = display_path(source_path(r, source));
  size_t table = rt->sp;
  size_t first = characters(text, 0, start);
  bool ok =
      path && push(rt, oti_table_new(rt)) && push_text(rt, path) && put(rt, table, ":path") &&
      push(rt, ot_int((intptr_t)line)) && put(rt, table, ":line") &&
      push(rt, ot_int((intptr_t)characters(text, line_start, start) + 1)) &&
      put(rt, table, ":column") && push(rt, ot_int((intptr_t)first)) && put(rt, table, ":start") &&
      push(rt, ot_int((intptr_t)(first + characters(text, start, end)))) && put(rt, table, ":end");
  free(path);
  return ok;
}

static bool boundary(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v' ||
         strchr("()[]{}'`,\";", c);
}

/* The byte offset of the first token name within the form at start, or
 * SIZE_MAX. */
static size_t token_in_form(oti_report* r, uint32_t source, size_t start, const char* name) {
  size_t length, end, n = strlen(name), ignored;
  const char* text = source_text(r, source, &length);
  const char* message;
  if (!text || !n || start >= length ||
      ot_scan_form(text, length, start, &end, &message, &ignored) != OT_FORM_COMPLETE)
    return SIZE_MAX;
  for (size_t i = start; i + n <= end; i++)
    if (!memcmp(text + i, name, n) && (i == 0 || boundary(text[i - 1])) &&
        (i + n == length || boundary(text[i + n])))
      return i;
  return SIZE_MAX;
}

static size_t edit_distance(const char* a, const char* b) {
  size_t n = strlen(a), m = strlen(b);
  if ((n > m ? n - m : m - n) > 2 || m > 255) return 3;
  size_t row[256];
  for (size_t j = 0; j <= m; j++) row[j] = j;
  for (size_t i = 1; i <= n; i++) {
    size_t diagonal = row[0];
    row[0] = i;
    for (size_t j = 1; j <= m; j++) {
      size_t above = row[j], best = above + 1;
      if (row[j - 1] + 1 < best) best = row[j - 1] + 1;
      if (diagonal + (a[i - 1] != b[j - 1]) < best) best = diagonal + (a[i - 1] != b[j - 1]);
      row[j] = best;
      diagonal = above;
    }
  }
  return row[m];
}

static int compare_suggestions(const void* a, const void* b) {
  return strcmp(*(const char* const*)a, *(const char* const*)b);
}

/* Push an array of up to three close spellings of the name of e among the
 * globals visible in its namespace, qualified as it is, or nil. */
static bool push_suggestions(ot_state* rt, const oti_error_record* e) {
  const char* close[64];
  const char *name = e->name + e->qualifier, *prefix = e->name;
  uint32_t space = e->space;
  size_t count = 0, limit = strlen(name) < 5 ? 1 : 2;
  for (size_t i = 0; i < rt->global_count && count < 64; i++) {
    const oti_global_info* info = &rt->global_info[i];
    if ((info->space != space && (info->space != 0 || info->is_private || e->qualifier)) ||
        oti_unbound(oti_global_value(rt, (uint32_t)i)) || !strcmp(info->name, name) ||
        edit_distance(name, info->name) > limit)
      continue;
    bool seen = false;
    for (size_t j = 0; j < count && !seen; j++) seen = !strcmp(close[j], info->name);
    if (!seen) close[count++] = info->name;
  }
  if (!count) return push(rt, OT_NIL);
  qsort(close, count, sizeof(close[0]), compare_suggestions);
  size_t base = rt->sp;
  for (size_t i = 0; i < count && i < 3; i++) {
    size_t n = e->qualifier + strlen(close[i]);
    char* text = malloc(n + 1);
    if (!text) return false;
    memcpy(text, prefix, e->qualifier);
    memcpy(text + e->qualifier, close[i], n - e->qualifier + 1);
    bool ok = push_text(rt, text);
    free(text);
    if (!ok) return false;
  }
  return push_array(rt, base);
}

/* Add :span, :frames, :notes and :suggestions to the diagnostic in slot d,
 * from where e happened. */
static bool locate(oti_report* r, const oti_error_record* e, size_t d) {
  ot_state* rt = r->rt;
  ot_value existing = field_get(rt, d, ":span");
  if (!existing) return false;
  if (existing != OT_NIL) return true;
  /* The condition keeps the code once handlers have seen the error. */
  char code[sizeof(e->code)] = "";
  ot_value code_value = field_get(rt, d, ":code");
  if (!code_value) return false;
  if (oti_kind(code_value, K_STRING)) {
    size_t n;
    const char* text = oti_text_bytes(code_value, &n);
    if (n < sizeof(code)) {
      memcpy(code, text, n);
      code[n] = 0;
    }
  }
  bool ok = true;
  const oti_error_place* inner = e->frame_count ? &e->frames[0] : NULL;
  oti_error_place at = {e->source, (uint32_t)e->offset, NULL};
  if (!inner && e->located) inner = &at;
  size_t token = SIZE_MAX;
  if (e->name && !strcmp(code, "E0201")) {
    ok = push_suggestions(rt, e) && put(rt, d, ":suggestions");
    if (inner) token = token_in_form(r, inner->source, inner->offset, e->name);
  }

  if (!ok) return false;
  if (token != SIZE_MAX) {
    ok = push_span(r, inner->source, token, token + strlen(e->name)) && put(rt, d, ":span");
  } else if (e->frame_count) {
    ok = push_span(r, inner->source, inner->offset, 0) && put(rt, d, ":span");
    size_t base = rt->sp;
    for (size_t i = 0; ok && i < e->frame_count; i++) {
      size_t frame = rt->sp;
      ok = push(rt, oti_table_new(rt)) &&
           (e->frames[i].name ? push_text(rt, e->frames[i].name) : push(rt, OT_NIL)) &&
           put(rt, frame, ":name") && push_span(r, e->frames[i].source, e->frames[i].offset, 0) &&
           put(rt, frame, ":span");
    }
    ok = ok && push_array(rt, base) && put(rt, d, ":frames");
  } else if (e->located) {
    /* A read error points at the character where the problem starts. */
    bool read = !strncmp(code, "E00", 3);

    ok = push_span(r, e->source, e->offset, read ? e->offset + 1 : 0) && put(rt, d, ":span");
  }
  if (!ok || !e->reached_count) return ok;
  size_t base = rt->sp;
  for (size_t i = 0; ok && i < e->reached_count; i++) {
    size_t length;
    const char* text = source_text(r, e->reached[i].source, &length);
    const char* path = source_path(r, e->reached[i].source);
    char* shown = path ? display_path(path) : NULL;
    if (!text || !shown || e->reached[i].offset > length) {
      free(shown);
      continue;
    }
    size_t line = 1, line_start = 0;
    for (size_t j = 0; j < e->reached[i].offset; j++)
      if (text[j] == '\n') {
        line++;
        line_start = j + 1;
      }
    char note[64];
    size_t column = characters(text, line_start, e->reached[i].offset) + 1;
    snprintf(note, sizeof(note), ":%zu:%zu", line, column);
    size_t n = strlen(shown), m = strlen(note);
    char* full = malloc(sizeof("reached from ") + n + m);
    if (full) {
      memcpy(full, "reached from ", 13);
      memcpy(full + 13, shown, n);
      memcpy(full + 13 + n, note, m + 1);
      ok = push_text(rt, full);
    }
    free(full);
    free(shown);
  }
  return ok && push_array(rt, base) && put(rt, d, ":notes");
}

/* Render the diagnostic in values[0] with otium.diagnostics into values[1].
 * Loading and calling it reuse the VM stack from the bottom, so the values
 * are rooted where they are. */
static bool render(ot_state* rt, ot_value* values) {
  oti_ast name = {.kind = AST_SYMBOL, .as.text = {"otium.diagnostics", 17}};
  uint32_t space = rt->current_namespace;
  bool ok = oti_require(rt, &name);
  rt->current_namespace = space;
  ot_value result;
  ok = ok && ot_call(rt, "otium.diagnostics/render", &values[0], 1, &result) &&
       oti_kind(result, K_STRING);
  if (ok) values[1] = result;
  return ok;
}

bool ot_set_source_path(ot_state* rt, const char* path) {
  char* copy = NULL;
  if (path) {
    size_t n = strlen(path) + 1;
    if (!(copy = malloc(n))) return oti_error(rt, "out of host memory");
    memcpy(copy, path, n);
  }
  free(rt->host_path);
  rt->host_path = copy;
  return true;
}

bool ot_report_error(ot_state* rt, const char* path, FILE* out) {
  if (!rt->error.set || rt->interrupted || rt->exit_pending) return false;
  oti_report r = {.rt = rt, .host_path = path};
  bool raised = rt->raised;
  ot_value saved[] = {rt->condition_values[0]};
  oti_roots saved_root;
  oti_roots_push(&rt->heap, &saved_root, saved, 1);
  oti_error_record e;
  oti_roots error_roots;
  oti_take_error(rt, &e, &error_roots);
  rt->raised = false;
  rt->condition_values[0] = OT_NIL;
  size_t height = rt->sp, d = height;
  ot_value condition = raised ? saved[0] : oti_condition_of(rt, &e);
  bool ok = condition && oti_kind(condition, K_TABLE) && push(rt, condition) && locate(&r, &e, d);
  ot_value values[] = {ok ? rt->stack[d] : OT_NIL, OT_NIL};
  oti_roots values_root;
  oti_roots_push(&rt->heap, &values_root, values, 2);
  drop_to(rt, height);
  char* failure = NULL;
  if (ok) {
    oti_clear_error(rt);
    ok = render(rt, values);
    if (ok) {
      size_t n;
      const char* text = oti_text_bytes(values[1], &n);
      fwrite(text, 1, n, out);
    } else if (oti_failed(rt)) failure = copy_string(ot_error(rt));
  }
  oti_roots_pop(&rt->heap, &values_root);
  if (!ok) {
    fprintf(out, "error: %s\n", e.message);
    if (failure) fprintf(out, "note: otium.diagnostics could not render this error: %s\n", failure);
  }
  fflush(out);
  free(failure);
  for (size_t i = 0; i < r.text_count; i++) free(r.texts[i].text);
  drop_to(rt, height);
  oti_restore_error(rt, &e, &error_roots);
  rt->raised = raised;
  rt->condition_values[0] = saved[0];
  oti_roots_pop(&rt->heap, &saved_root);
  return true;
}
