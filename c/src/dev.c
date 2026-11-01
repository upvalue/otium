/* Primitives for the development server in otium.dev. The socket transport
 * is native/dev/transport.c; lib/otium/dev.ot handles the protocol and
 * c/src/dev.ot adapts these to the names it uses. */
#include "internal.h"
/* Angle brackets keep the transport out of the amalgamation. */
#include <dev/transport.h>
#include <string.h>
#include <stdlib.h>
#ifdef OTIUM_FREESTANDING
#include <time.h>
#endif

/* A string argument as a C string, or NULL after recording an error. */
static const char* dev_path(ot_state* rt, ot_value v) {
  if (!oti_kind(v, K_STRING) || memchr(oti_bytes_data(v), 0, oti_bytes_length(v))) {
    oti_error(rt, "expected a string without NUL");
    return NULL;
  }
  return (const char*)oti_bytes_data(v);
}

static ot_value dev_string(ot_state* rt, const char* text) {
  return oti_text(rt, K_STRING, text, strlen(text));
}

static ot_value dev_integer(ot_state* rt, const char* who, size_t first, size_t count,
                            intptr_t* out) {
  if (!oti_check_arity(rt, who, count, 1, 1)) return 0;
  if (!ot_is_int(rt->stack[first])) {
    oti_error(rt, "expected an integer");
    return 0;
  }
  *out = ot_to_int(rt->stack[first]);
  return OT_TRUE;
}

static ot_value dev_open(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "otium.dev/%open", count, 1, 1)) return 0;
  const char* path = dev_path(rt, rt->stack[first]);
  return path ? ot_bool(ot_dev_open(path) == 0) : 0;
}

static ot_value dev_close(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "otium.dev/%close", count, 0, 0)) return 0;
  ot_dev_close();
  return OT_NIL;
}

static ot_value dev_poll(ot_state* rt, size_t first, size_t count) {
  intptr_t timeout;
  if (!dev_integer(rt, "otium.dev/%transport-poll", first, count, &timeout)) return 0;
  if (timeout < -1 || timeout > INT32_MAX) {
    oti_error(rt, "poll timeout out of range");
    return 0;
  }
  return ot_int(ot_dev_poll((int)timeout));
}

static ot_value dev_fds(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "otium.dev/%transport-fds", count, 0, 0)) return 0;
  size_t n = ot_dev_fds(NULL, 0), base = rt->sp;
  int* pairs = n ? malloc(n * sizeof(int)) : NULL;
  if (n && !pairs) return oti_fail(rt, "out of host memory");
  ot_dev_fds(pairs, n);
  if (!oti_stack(rt, base + n)) {
    free(pairs);
    return 0;
  }
  for (size_t i = 0; i < n; i++) rt->stack[base + i] = ot_int(pairs[i]);
  free(pairs);
  rt->sp = base + n;
  ot_value result = oti_array_from(rt, base, n);
  rt->sp = base;
  return result;
}

static ot_value dev_next(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "otium.dev/%next", count, 0, 0)) return 0;
  const char* text = ot_dev_next();
  return text ? dev_string(rt, text) : OT_FALSE;
}

static ot_value dev_client(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "otium.dev/%client", count, 0, 0)) return 0;
  return ot_int(ot_dev_client());
}

static ot_value dev_send(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "otium.dev/%send", count, 2, 2)) return 0;
  if (!ot_is_int(rt->stack[first])) {
    oti_error(rt, "expected a client id");
    return 0;
  }
  const char* text = dev_path(rt, rt->stack[first + 1]);
  if (!text) return 0;
  ot_dev_send((int)ot_to_int(rt->stack[first]), text);
  return OT_NIL;
}

static ot_value dev_clock(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "otium.dev/%clock", count, 0, 0)) return 0;
  return ot_int((intptr_t)ot_dev_clock());
}

static ot_value dev_now(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "otium.dev/%now", count, 0, 0)) return 0;
  return ot_int((intptr_t)ot_dev_now());
}

static ot_value dev_pid(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "otium.dev/%pid", count, 0, 0)) return 0;
  return ot_int(ot_dev_pid());
}

static ot_value dev_uid(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "otium.dev/%uid", count, 0, 0)) return 0;
  return ot_int(ot_dev_uid());
}

static ot_value dev_cwd(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "otium.dev/%cwd", count, 0, 0)) return 0;
  return dev_string(rt, ot_dev_cwd());
}

static ot_value dev_private_directory(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "otium.dev/%private-directory!", count, 1, 1)) return 0;
  const char* path = dev_path(rt, rt->stack[first]);
  return path ? ot_bool(ot_dev_private_directory(path)) : 0;
}

static ot_value dev_realpath(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "otium.dev/%realpath", count, 1, 1)) return 0;
  const char* path = dev_path(rt, rt->stack[first]);
  return path ? dev_string(rt, ot_dev_realpath(path)) : 0;
}

static ot_value dev_mtime(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "otium.dev/%mtime", count, 1, 1)) return 0;
  const char* path = dev_path(rt, rt->stack[first]);
  if (!path) return 0;
  int64_t mtime = ot_dev_mtime(path);
  return mtime < 0 ? OT_FALSE : ot_int((intptr_t)mtime);
}

/* Push value onto the list in *list. Both must be rooted by the caller. */
static bool dev_push(ot_state* rt, ot_value* list, ot_value* value) {
  ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
  if (!pair) return false;
  oti_as_pair(pair)->car = *value;
  oti_as_pair(pair)->cdr = *list;
  *list = pair;
  return true;
}

static ot_value dev_namespace_names(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!oti_check_arity(rt, "otium.dev/%namespace-names", count, 0, 0)) return 0;
  ot_value v[] = {OT_EMPTY, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, v, 2);
  bool ok = true;
  for (size_t i = rt->namespace_count; ok && i; i--) {
    const char* name = rt->namespaces[i - 1];
    ok = (v[1] = oti_intern(rt, K_SYMBOL, name, strlen(name))) && dev_push(rt, &v[0], &v[1]);
  }
  oti_roots_pop(&rt->heap, &roots);
  return ok ? v[0] : 0;
}

/* An arity mask, as in procedure-arity-mask: bit n is set when n arguments are accepted,
 * and the mask is negative when any number above the required count is. */
static ot_value arity_mask(ot_state* rt, ot_value value) {
  if (!oti_kind(value, K_CLOSURE)) return OT_FALSE;
  const oti_function* fn = oti_closure_fn(rt, value);
  if (!fn) return OT_FALSE;
  if (fn->arity + fn->optional > 60) return OT_FALSE;
  if (fn->rest) return ot_int(-((intptr_t)1 << fn->arity));
  return ot_int((((intptr_t)1 << (fn->optional + 1)) - 1) << fn->arity);
}

/* Each binding defined in a namespace as (name kind public? doc arity-mask),
 * where arity-mask is #f for anything but an Otium function. */
static ot_value dev_namespace_bindings(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "otium.dev/%namespace-bindings", count, 1, 1)) return 0;
  ot_value name = rt->stack[first];
  if (!oti_kind(name, K_SYMBOL)) {
    oti_error(rt, "bindings: expected a namespace symbol");
    return 0;
  }
  size_t length;
  const char* text = oti_text_bytes(name, &length);
  uint32_t space = UINT32_MAX;
  for (size_t i = 0; i < rt->namespace_count; i++)
    if (strlen(rt->namespaces[i]) == length && !memcmp(rt->namespaces[i], text, length))
      space = (uint32_t)i;
  if (space == UINT32_MAX) {
    oti_error(rt, "bindings: unknown namespace");
    return 0;
  }
  /* v: the result, one entry being built, and the field being added. */
  ot_value v[] = {OT_EMPTY, OT_EMPTY, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, v, 3);
  bool ok = true;
  for (size_t i = 0; ok && i < rt->global_count; i++) {
    const oti_global_info* info = &rt->global_info[i];
    if (info->space != space || !info->defined || oti_unbound(oti_global_value(rt, (uint32_t)i)))
      continue;
    const char* kind = info->macro                                        ? ":macro"
                       : oti_procedure(oti_global_value(rt, (uint32_t)i)) ? ":function"
                                                                          : ":value";
    v[1] = OT_EMPTY;
    v[2] = arity_mask(rt, oti_global_value(rt, (uint32_t)i));
    ok = dev_push(rt, &v[1], &v[2]);
    ok = ok &&
         (v[2] = oti_text(rt, K_STRING, info->doc.text ? info->doc.text : "",
                          info->doc.text ? info->doc.length : 0)) &&
         dev_push(rt, &v[1], &v[2]);
    v[2] = ot_bool(!rt->global_info[i].is_private);
    ok = ok && dev_push(rt, &v[1], &v[2]);
    ok = ok && (v[2] = oti_intern(rt, K_KEYWORD, kind, strlen(kind))) && dev_push(rt, &v[1], &v[2]);
    ok = ok &&
         (v[2] =
              oti_intern(rt, K_SYMBOL, rt->global_info[i].name, strlen(rt->global_info[i].name))) &&
         dev_push(rt, &v[1], &v[2]);
    ok = ok && dev_push(rt, &v[0], &v[1]);
  }
  oti_roots_pop(&rt->heap, &roots);
  return ok ? v[0] : 0;
}

/* Catalog pages in stable runtime index order, not sorted. space=-1 lists
 * namespaces; otherwise it is a namespace ID. end=-1 captures a high watermark.
 * Result (next end rows); row (id bounded-name kind public? truncated?). */
static ot_value dev_catalog(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "otium.dev/%catalog", count, 3, 3)) return 0;
  for (size_t i = 0; i < 3; i++)
    if (!ot_is_int(rt->stack[first + i])) return oti_fail(rt, "catalog expects integer cursors");
  intptr_t space = ot_to_int(rt->stack[first]), start = ot_to_int(rt->stack[first + 1]);
  intptr_t stop = ot_to_int(rt->stack[first + 2]);
  if (space < -1 || (space >= 0 && (size_t)space >= rt->namespace_count))
    return oti_fail(rt, "unknown namespace ID");
  size_t total = space == -1 ? rt->namespace_count : rt->global_count;
  if (stop == -1) stop = (intptr_t)total;
  if (start < 0 || stop < start || (size_t)stop > total)
    return oti_fail(rt, "invalid catalog cursor");
  ot_value live[] = {OT_EMPTY, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 3);
  size_t at = (size_t)start, scanned = 0, found = 0;
  bool ok = true;
  while (ok && at < (size_t)stop && scanned++ < 512 && found < 64) {
    size_t id = at++;
    const char* name;
    size_t length;
    bool is_public = true, truncated = false;
    const char* kind = "namespace";
    if (space == -1) {
      name = rt->namespaces[id];
      length = strnlen(name, 257);
      truncated = length > 256;
      if (truncated) length = 256;
    } else {
      const oti_global_info* info = &rt->global_info[id];
      if (info->space != (uint32_t)space || !info->defined ||
          oti_unbound(oti_global_value(rt, (uint32_t)id)))
        continue;
      name = info->name;
      length = info->length < 256 ? info->length : 256;
      truncated = info->length > 256;
      is_public = !info->is_private;
      kind = info->macro                                         ? "macro"
             : oti_procedure(oti_global_value(rt, (uint32_t)id)) ? "function"
                                                                 : "value";
    }
    /* Names are previews, never used to resolve the selected binding. */
    while (length && ((unsigned char)name[length] & 0xc0) == 0x80) length--;
    live[1] = oti_text(rt, K_STRING, name, length);
    live[2] = live[1] ? dev_string(rt, kind) : 0;
    if (live[2]) {
      ot_value tail = oti_cons(rt, ot_bool(truncated), OT_EMPTY);
      if (tail) tail = oti_cons(rt, ot_bool(is_public), tail);
      live[2] = tail ? oti_cons(rt, live[2], tail) : 0;
    }
    if (live[2]) live[2] = oti_cons(rt, live[1], live[2]);
    if (live[2]) live[2] = oti_cons(rt, ot_int((intptr_t)id), live[2]);
    ok = live[2] && dev_push(rt, &live[0], &live[2]);
    found++;
  }
  if (ok) {
    live[0] = oti_cons(rt, live[0], OT_EMPTY);
    if (live[0]) live[0] = oti_cons(rt, ot_int(stop), live[0]);
    if (live[0]) live[0] = oti_cons(rt, ot_int((intptr_t)at), live[0]);
  }
  ot_value result = ok ? live[0] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value dev_binding_value(ot_state* rt, size_t first, size_t count) {
  intptr_t id;
  if (!dev_integer(rt, "otium.dev/%binding-value", first, count, &id)) return 0;
  if (id < 0 || (size_t)id >= rt->global_count || !rt->global_info[id].defined ||
      oti_unbound(oti_global_value(rt, (uint32_t)id)))
    return oti_fail(rt, "binding no longer available");
  return oti_global_value(rt, (uint32_t)id);
}

/* The same completeness scanner used by the CLI. Bounded input, no evaluation. */
static ot_value dev_scan_source(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "otium.dev/%scan-source", count, 1, 1)) return 0;
  if (!oti_kind(rt->stack[first], K_STRING)) return oti_fail(rt, "expected source string");
  size_t length = oti_bytes_length(rt->stack[first]);
  if (length > 16384) return oti_fail(rt, "REPL input exceeds 16 KiB");
  const char* source = (const char*)oti_bytes_data(rt->stack[first]);
  size_t start = 0, end, error_offset;
  const char* error;
  for (;;) {
    ot_form_status status = ot_scan_form(source, length, start, &end, &error, &error_offset);
    if (status == OT_FORM_END) return OT_TRUE;
    if (status == OT_FORM_INCOMPLETE) return OT_FALSE;
    if (status == OT_FORM_ERROR) return oti_fail(rt, error);
    start = end;
  }
}

/* Evaluate a datum in the named namespace, creating it if needed. A ns form
 * inside it changes the namespace only for that evaluation. */
static ot_value dev_eval_in(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "otium.dev/%eval-in", count, 2, 2)) return 0;
  if (!oti_kind(rt->stack[first], K_SYMBOL)) {
    oti_error(rt, "expected a namespace symbol");
    return 0;
  }
  size_t length;
  const char* name = oti_text_bytes(rt->stack[first], &length);
  uint32_t space = oti_namespace(rt, name, length);
  if (space == UINT32_MAX) return 0;
  uint32_t saved = rt->current_namespace;
  rt->current_namespace = space;
  ot_value result = oti_eval_value(rt, rt->stack[first + 1]);
  rt->current_namespace = saved;
  return result;
}

/* Compile while in C, then let Otium call the resulting thunk so eval can
 * park. Namespace/import/macro declarations finish during preparation. */
static ot_value dev_prepare_in(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "otium.dev/%prepare-in", count, 2, 2)) return 0;
  if (!oti_kind(rt->stack[first], K_SYMBOL)) return oti_fail(rt, "expected a namespace symbol");
  size_t length;
  const char* name = oti_text_bytes(rt->stack[first], &length);
  uint32_t space = oti_namespace(rt, name, length);
  if (space == UINT32_MAX) return 0;
  rt->current_namespace = space;
  int prepared;
  ot_value result = oti_prepare_value(rt, rt->stack[first + 1], &prepared);
  if (!result) return 0;
  rt->stack[first] = ot_int(prepared);
  rt->stack[first + 1] = result;
  return oti_array_from(rt, first, 2);
}

/* Replace a param's default, the value it has outside any with-params. The
 * server sends output printed outside requests to clients this way, since
 * current-output-port's cell in otium.core can't be redefined. */
static ot_value dev_set_param_default(ot_state* rt, size_t first, size_t count) {
  if (!oti_check_arity(rt, "otium.dev/%set-param-default!", count, 2, 2)) return 0;
  if (!oti_kind(rt->stack[first], K_PARAM)) {
    oti_error(rt, "expected a param");
    return 0;
  }
  oti_as_param(rt->stack[first])->value = rt->stack[first + 1];
  return OT_NIL;
}

#ifdef OTIUM_FREESTANDING
/* Without an operating system there is no socket or file system: the server
 * can't listen, and the rest answer as for a process with nothing to report. */
int ot_dev_open(const char* path) {
  (void)path;
  return -1;
}

void ot_dev_close(void) {}

size_t ot_dev_fds(int* pairs, size_t capacity) {
  (void)pairs;
  (void)capacity;
  return 0;
}

int ot_dev_poll(int timeout) {
  (void)timeout;
  return 0;
}

const char* ot_dev_next(void) { return NULL; }

int ot_dev_client(void) { return -1; }

void ot_dev_send(int client, const char* text) {
  (void)client;
  (void)text;
}

uint64_t ot_dev_clock(void) {
  struct timespec t;
  return clock_gettime(CLOCK_MONOTONIC, &t)
             ? 0
             : (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}

int ot_dev_uid(void) { return 0; }

int ot_dev_private_directory(const char* path) {
  (void)path;
  return 0;
}

int ot_dev_pid(void) { return 0; }

const char* ot_dev_cwd(void) { return "/"; }

int64_t ot_dev_now(void) {
  struct timespec t;
  return clock_gettime(CLOCK_REALTIME, &t) ? 0 : (int64_t)t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

const char* ot_dev_realpath(const char* path) {
  (void)path;
  return NULL;
}

int64_t ot_dev_mtime(const char* path) {
  (void)path;
  return -1;
}
#endif

const oti_builtin_def oti_dev_builtins[] = {
    {B_DEV_CATALOG, "otium.dev/%catalog", dev_catalog},
    {B_DEV_BINDING_VALUE, "otium.dev/%binding-value", dev_binding_value},
    {B_DEV_SCAN_SOURCE, "otium.dev/%scan-source", dev_scan_source},
    {B_DEV_PREPARE_IN, "otium.dev/%prepare-in", dev_prepare_in},
    {B_DEV_EVAL_IN, "otium.dev/%eval-in", dev_eval_in},
    {B_DEV_SET_PARAM_DEFAULT, "otium.dev/%set-param-default!", dev_set_param_default},
    {B_DEV_OPEN, "otium.dev/%open", dev_open},
    {B_DEV_CLOSE, "otium.dev/%close", dev_close},
    {B_DEV_FDS, "otium.dev/%transport-fds", dev_fds},
    {B_DEV_POLL, "otium.dev/%transport-poll", dev_poll},
    {B_DEV_NEXT, "otium.dev/%next", dev_next},
    {B_DEV_CLIENT, "otium.dev/%client", dev_client},
    {B_DEV_SEND, "otium.dev/%send", dev_send},
    {B_DEV_CLOCK, "otium.dev/%clock", dev_clock},
    {B_DEV_NOW, "otium.dev/%now", dev_now},
    {B_DEV_PID, "otium.dev/%pid", dev_pid},
    {B_DEV_UID, "otium.dev/%uid", dev_uid},
    {B_DEV_CWD, "otium.dev/%cwd", dev_cwd},
    {B_DEV_PRIVATE_DIRECTORY, "otium.dev/%private-directory!", dev_private_directory},
    {B_DEV_REALPATH, "otium.dev/%realpath", dev_realpath},
    {B_DEV_MTIME, "otium.dev/%mtime", dev_mtime},
    {B_DEV_NAMESPACE_NAMES, "otium.dev/%namespace-names", dev_namespace_names},
    {B_DEV_NAMESPACE_BINDINGS, "otium.dev/%namespace-bindings", dev_namespace_bindings},
    {0, NULL, NULL},
};
