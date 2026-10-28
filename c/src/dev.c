/* Primitives for the development server in otium.dev. The socket transport
 * is native/dev/transport.c; lib/otium/dev.ot handles the protocol and
 * c/src/dev.ot adapts these to the names it uses. */
#include "internal.h"
/* Angle brackets keep the transport out of the amalgamation. */
#include <dev/transport.h>
#include <string.h>
#ifdef OTIUM_FREESTANDING
#include <time.h>
#endif

static bool dev_arity(ot_state* rt, size_t count, size_t expected) {
  if (count == expected) return true;
  return oti_errorf(rt, "development primitive expects %zu argument%s", expected,
                    expected == 1 ? "" : "s");
}

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

static ot_value dev_integer(ot_state* rt, size_t first, size_t count, intptr_t* out) {
  if (!dev_arity(rt, count, 1)) return 0;
  if (!ot_is_int(rt->stack[first])) {
    oti_error(rt, "expected an integer");
    return 0;
  }
  *out = ot_to_int(rt->stack[first]);
  return OT_TRUE;
}

static ot_value dev_open(ot_state* rt, size_t first, size_t count) {
  if (!dev_arity(rt, count, 1)) return 0;
  const char* path = dev_path(rt, rt->stack[first]);
  return path ? ot_bool(ot_dev_open(path) == 0) : 0;
}

static ot_value dev_close(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!dev_arity(rt, count, 0)) return 0;
  ot_dev_close();
  return OT_NIL;
}

static ot_value dev_poll(ot_state* rt, size_t first, size_t count) {
  intptr_t timeout;
  if (!dev_integer(rt, first, count, &timeout)) return 0;
  if (timeout < -1 || timeout > INT32_MAX) {
    oti_error(rt, "poll timeout out of range");
    return 0;
  }
  return ot_int(ot_dev_poll((int)timeout));
}

static ot_value dev_next(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!dev_arity(rt, count, 0)) return 0;
  const char* text = ot_dev_next();
  return text ? dev_string(rt, text) : OT_FALSE;
}

static ot_value dev_client(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!dev_arity(rt, count, 0)) return 0;
  return ot_int(ot_dev_client());
}

static ot_value dev_send(ot_state* rt, size_t first, size_t count) {
  if (!dev_arity(rt, count, 2)) return 0;
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
  if (!dev_arity(rt, count, 0)) return 0;
  return ot_int((intptr_t)ot_dev_clock());
}

static ot_value dev_now(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!dev_arity(rt, count, 0)) return 0;
  return ot_int((intptr_t)ot_dev_now());
}

static ot_value dev_pid(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!dev_arity(rt, count, 0)) return 0;
  return ot_int(ot_dev_pid());
}

static ot_value dev_uid(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!dev_arity(rt, count, 0)) return 0;
  return ot_int(ot_dev_uid());
}

static ot_value dev_cwd(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (!dev_arity(rt, count, 0)) return 0;
  return dev_string(rt, ot_dev_cwd());
}

static ot_value dev_private_directory(ot_state* rt, size_t first, size_t count) {
  if (!dev_arity(rt, count, 1)) return 0;
  const char* path = dev_path(rt, rt->stack[first]);
  return path ? ot_bool(ot_dev_private_directory(path)) : 0;
}

static ot_value dev_realpath(ot_state* rt, size_t first, size_t count) {
  if (!dev_arity(rt, count, 1)) return 0;
  const char* path = dev_path(rt, rt->stack[first]);
  return path ? dev_string(rt, ot_dev_realpath(path)) : 0;
}

static ot_value dev_mtime(ot_state* rt, size_t first, size_t count) {
  if (!dev_arity(rt, count, 1)) return 0;
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
  if (!dev_arity(rt, count, 0)) return 0;
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
  if (!dev_arity(rt, count, 1)) return 0;
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
    if (info->space != space || !info->defined || oti_unbound(oti_global_value(rt, (uint32_t)i))) continue;
    const char* kind = info->macro                     ? ":macro"
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

/* Evaluate a datum in the named namespace, creating it if needed. A ns form
 * inside it changes the namespace only for that evaluation. */
static ot_value dev_eval_in(ot_state* rt, size_t first, size_t count) {
  if (!dev_arity(rt, count, 2)) return 0;
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

#ifdef OTIUM_FREESTANDING
/* Without an operating system there is no socket or file system: the server
 * can't listen, and the rest answer as for a process with nothing to report. */
int ot_dev_open(const char* path) {
  (void)path;
  return -1;
}

void ot_dev_close(void) {}

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
    {B_DEV_EVAL_IN, "otium.dev/%eval-in", dev_eval_in},
    {B_DEV_OPEN, "otium.dev/%open", dev_open},
    {B_DEV_CLOSE, "otium.dev/%close", dev_close},
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
