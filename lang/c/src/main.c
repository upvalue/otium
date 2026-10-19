#define _POSIX_C_SOURCE 200809L
#define OT_INTERNAL
#include "otium.h"

#include "line/line-edit.h"
#include "dev/transport.h"
static const char ot_dev_src[] = {
#embed "../../lib/otium/dev.ot"
};
static const unsigned ot_dev_src_len = sizeof ot_dev_src;

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct strings {
  char** values;
  size_t length;
  size_t capacity;
} strings;

typedef struct bytes {
  char* data;
  size_t length;
  size_t capacity;
} bytes;

static ots* signal_state;
static volatile sig_atomic_t saw_interrupt;
static strings load_path;

static void bytes_reserve(bytes* buffer, size_t extra) {
  if (extra <= buffer->capacity - buffer->length) return;
  size_t capacity = buffer->capacity == 0 ? 256 : buffer->capacity;
  while (capacity < buffer->length + extra) capacity *= 2;
  char* grown = ot_host_realloc(buffer->data, capacity);
  if (grown == NULL) abort();
  buffer->data = grown;
  buffer->capacity = capacity;
}

static void bytes_append(bytes* buffer, const char* data, size_t length) {
  bytes_reserve(buffer, length);
  memcpy(buffer->data + buffer->length, data, length);
  buffer->length += length;
}

static char* copy_string(const char* value, size_t length) {
  char* copy = ot_host_alloc(length + 1);
  if (copy == NULL) abort();
  memcpy(copy, value, length);
  copy[length] = '\0';
  return copy;
}

static void strings_push(strings* list, const char* value, size_t length) {
  if (list->length == list->capacity) {
    size_t capacity = list->capacity == 0 ? 8 : list->capacity * 2;
    void* grown = ot_host_realloc(list->values, capacity * sizeof(*list->values));
    if (grown == NULL) abort();
    list->values = grown;
    list->capacity = capacity;
  }
  list->values[list->length++] = copy_string(value, length);
}

static void handle_interrupt(int signal_number) {
  (void)signal_number;
  saw_interrupt = 1;
  if (signal_state != NULL) ot_interrupt(signal_state);
}

static void stdout_writer(void* userdata, const char* data, size_t length) {
  (void)userdata;
  fwrite(data, 1, length, stdout);
}

static bool record_loaded(ots* state, const char* name, const char* path);

static bool path_read(ots* state, const char* name, const char* directory, const char* relative,
                      char** source, size_t* length) {
  bytes path = {0};
  bytes_append(&path, directory, strlen(directory));
  if (path.length != 0 && path.data[path.length - 1] != '/') bytes_append(&path, "/", 1);
  bytes_append(&path, relative, strlen(relative));
  bytes_append(&path, "\0", 1);
  bool found = ot_platform_read_file(path.data, source, length);
  if (found && !record_loaded(state, name, path.data)) {
    ot_host_free(*source);
    *source = NULL;
    found = false;
  }
  ot_host_free(path.data);
  return found;
}

static bool module_loader(void* userdata, const char* namespace_name, char** source,
                          size_t* length) {
  bytes relative = {0};
  for (const char* cursor = namespace_name; *cursor != '\0'; cursor++) {
    char byte = *cursor == '.' ? '/' : *cursor;
    bytes_append(&relative, &byte, 1);
  }
  size_t stem = relative.length;
  static const char* extensions[] = {".ot", ".scm"};
  for (size_t i = 0; i < load_path.length; i++)
    for (size_t j = 0; j < sizeof extensions / sizeof extensions[0]; j++) {
      relative.length = stem;
      bytes_append(&relative, extensions[j], strlen(extensions[j]));
      bytes_append(&relative, "\0", 1);
      if (path_read(userdata, namespace_name, load_path.values[i], relative.data, source, length)) {
        ot_host_free(relative.data);
        return true;
      }
      if (((ots*)userdata)->vm.unwind_kind != UNWIND_NONE) {
        ot_host_free(relative.data);
        return false;
      }
      relative.length = stem;
    }
  ot_host_free(relative.data);
  return false;
}

static void print_value(ots* state, otv value, FILE* stream) {
  ot_repr_to(state, value, false, ot_default_write, stream);
  fputc('\n', stream);
}

static const char* condition_message(ots* state, size_t* length) {
  otv condition = ot_condition(state);
  OT_FRAME_SCOPED(state, &condition);
  if (!ot_has_type(condition, OBJ_TABLE)) return NULL;
  otv key = ot_intern(state, "message", 7, true);
  otv message = ot_table_get(state, condition, key, ot_nil);
  const char* text;
  if (!ot_string_bytes(message, &text, length)) return NULL;
  return text;
}

static int run_file(ots* state, const char* path) {
  char* source;
  size_t length;
  if (!ot_platform_read_file(path, &source, &length)) {
    fprintf(stderr, "otium: cannot open %s\n", path);
    return 1;
  }
  const char* program = source;
  size_t program_length = length;
  otv result;
  bool ok = ot_eval_src(state, program, program_length, path, &result);
  ot_host_free(source);
  if (ok) return 0;
  if (state->vm.unwind_kind == UNWIND_QUIT) {
    ot_clear_condition(state);
    return state->quit_requested ? state->quit_status : 130;
  }
  fputs("otium: ", stderr);
  print_value(state, ot_condition(state), stderr);
  return 1;
}

static bool dev_enabled;
static bool dev_stdio;
static uint64_t dev_deadline, dev_last_poll;
static bytes* dev_capture;
static bytes dev_output;

static void dev_bytes_writer(void* userdata, const char* text, size_t length) {
  bytes_append(userdata, text, length);
}
/* Marshal the loader's borrowed strings into an Otium notification. The dev
 * module owns the registry and timestamp; no managed values are retained here. */
static bool record_loaded(ots* state, const char* name, const char* path) {
  if (!dev_enabled) return true;
  char* copied_name = copy_string(name, strlen(name));
  bytes code = {0};
  const char* prefix = "(otium.dev/record-load! (string->symbol ";
  bytes_append(&code, prefix, strlen(prefix));
  otv text = ot_make_string(state, copied_name, strlen(copied_name));
  ot_host_free(copied_name);
  ot_repr_to(state, text, false, dev_bytes_writer, &code);
  bytes_append(&code, ") ", 2);
  text = ot_make_string(state, path, strlen(path));
  ot_repr_to(state, text, false, dev_bytes_writer, &code);
  bytes_append(&code, ")", 1);
  otv ignored;
  bool ok = ot_eval_src(state, code.data, code.length, "<dev-load>", &ignored);
  ot_host_free(code.data);
  return ok;
}
static otv dev_repr(ots* s, otv value) {
  bytes text = {0};
  ot_repr_to(s, value, false, dev_bytes_writer, &text);
  otv result = ot_make_string(s, text.data, text.length);
  ot_host_free(text.data);
  return result;
}
static void dev_writer(void* userdata, const char* text, size_t length) {
  (void)userdata;
  if (dev_capture) bytes_append(dev_capture, text, length);
  else {
    fwrite(text, 1, length, stdout);
    if (dev_enabled) bytes_append(&dev_output, text, length);
  }
}
static otv dev_enabled_p(ots* s, otv* args, int argc) {
  (void)s;
  (void)args;
  (void)argc;
  return dev_enabled ? ot_true : ot_false;
}
static otv dev_clock(ots* s, otv* args, int argc) {
  (void)s;
  (void)args;
  (void)argc;
  return ot_make_int(ot_dev_clock());
}
static otv dev_pid(ots* s, otv* args, int argc) {
  (void)s;
  (void)args;
  (void)argc;
  return ot_make_int(ot_dev_pid());
}
static otv dev_uid(ots* s, otv* args, int argc) {
  (void)s;
  (void)args;
  (void)argc;
  return ot_make_int(ot_dev_uid());
}
static otv dev_path_call(ots* s, otv* args, int argc, int (*operation)(const char*), int success) {
  const char* text;
  size_t length;
  if (argc != 1 || !ot_string_bytes(args[0], &text, &length) || memchr(text, 0, length))
    return ot_raise(s, "dev: expected a path without NUL");
  char* path = copy_string(text, length);
  int result = operation(path);
  ot_host_free(path);
  return result == success ? ot_true : ot_false;
}
static otv dev_private_directory(ots* s, otv* args, int argc) {
  return dev_path_call(s, args, argc, ot_dev_private_directory, 1);
}
static otv dev_open(ots* s, otv* args, int argc) {
  return dev_path_call(s, args, argc, ot_dev_open, 0);
}
static otv dev_cwd(ots* s, otv* args, int argc) {
  (void)args;
  (void)argc;
  const char* path = ot_dev_cwd();
  return ot_make_string(s, path, strlen(path));
}
static otv dev_runtime(ots* s, otv* args, int argc) {
  (void)args;
  (void)argc;
  return ot_intern(s, "c", 1, true);
}
static otv dev_poll(ots* s, otv* args, int argc) {
  if (argc != 1 || !ot_is_int(args[0])) return ot_raise(s, "poll: expected milliseconds");
  return ot_make_int(ot_dev_poll((int)ot_get_int(args[0])));
}
static otv dev_next(ots* s, otv* args, int argc) {
  (void)args;
  (void)argc;
  const char* text = ot_dev_next();
  return text ? ot_make_string(s, text, strlen(text)) : ot_false;
}
static otv dev_client(ots* s, otv* args, int argc) {
  (void)s;
  (void)args;
  (void)argc;
  return ot_make_int(ot_dev_client());
}
static otv dev_send(ots* s, otv* args, int argc) {
  const char* text;
  size_t length;
  if (argc != 2 || !ot_is_int(args[0]) || !ot_string_bytes(args[1], &text, &length))
    return ot_raise(s, "send: expected client and string");
  char* copy = copy_string(text, length);
  ot_dev_send((int)ot_get_int(args[0]), copy);
  ot_host_free(copy);
  return ot_nil;
}
static otv dev_render(ots* s, otv* args, int argc) {
  if (argc != 1) return ot_raise(s, "repr: expected value");
  return dev_repr(s, args[0]);
}
static otv dev_set_deadline(ots* s, otv* args, int argc) {
  if (argc != 1 || !ot_is_int(args[0])) return ot_raise(s, "deadline: expected integer");
  intptr_t n = ot_get_int(args[0]);
  if (n >= 0) dev_deadline = (uint64_t)n;
  return ot_make_int(dev_deadline);
}
static void dev_field(ots* s, otv table, const char* name, otv value) {
  OT_FRAME_SCOPED(s, &table, &value);
  otv key = ot_intern(s, name, strlen(name), true);
  ot_table_put(s, table, key, value);
}
static otv dev_now(ots* s, otv* args, int argc) {
  (void)s;
  (void)args;
  (void)argc;
  return ot_make_int(ot_dev_now());
}
static otv dev_mtime(ots* s, otv* args, int argc) {
  const char* text;
  size_t length;
  if (argc != 1 || !ot_string_bytes(args[0], &text, &length))
    return ot_raise(s, "mtime: expected path");
  char* path = copy_string(text, length);
  int64_t mtime = ot_dev_mtime(path);
  ot_host_free(path);
  return mtime < 0 ? ot_false : ot_make_int(mtime);
}
static otv dev_realpath(ots* s, otv* args, int argc) {
  const char* text;
  size_t length;
  if (argc != 1 || !ot_string_bytes(args[0], &text, &length))
    return ot_raise(s, "realpath: expected path");
  char* path = copy_string(text, length);
  const char* resolved = ot_dev_realpath(path);
  otv result = ot_make_string(s, resolved, strlen(resolved));
  ot_host_free(path);
  return result;
}
static void dev_keyword_field(ots* s, otv table, const char* name, const char* keyword) {
  otv value = ot_nil;
  OT_FRAME_SCOPED(s, &table, &value);
  value = ot_intern(s, keyword, strlen(keyword), true);
  dev_field(s, table, name, value);
}
static void dev_string_field(ots* s, otv table, const char* name, const char* text) {
  otv value = ot_nil;
  OT_FRAME_SCOPED(s, &table, &value);
  value = ot_make_string(s, text, strlen(text));
  dev_field(s, table, name, value);
}
/* Copy the runtime's namespace names; Otium owns file/load-time metadata. */
static otv dev_namespaces(ots* s, otv* args, int argc) {
  (void)args;
  (void)argc;
  otv result = ot_null, cursor = s->namespaces, entry = ot_nil;
  OT_FRAME_SCOPED(s, &result, &cursor, &entry);
  while (ot_has_type(cursor, OBJ_NAMESPACE)) {
    entry = ot_table_new(s, 3);
    dev_field(s, entry, "name", ((ot_namespace_obj*)ot_as_obj(cursor))->name);
    result = ot_cons(s, entry, result);
    cursor = ((ot_namespace_obj*)ot_as_obj(cursor))->next;
  }
  return result;
}
/* Copy binding facts from opaque runtime objects. Otium derives public arity
 * metadata from the parameter list. */
static otv dev_bindings(ots* s, otv* args, int argc) {
  if (argc != 1 || !ot_has_type(args[0], OBJ_SYMBOL))
    return ot_raise(s, "bindings: expected namespace symbol");
  otv space = s->namespaces;
  while (ot_has_type(space, OBJ_NAMESPACE) &&
         ((ot_namespace_obj*)ot_as_obj(space))->name != args[0])
    space = ((ot_namespace_obj*)ot_as_obj(space))->next;
  if (!ot_has_type(space, OBJ_NAMESPACE)) return ot_raise(s, "bindings: unknown namespace");
  otv result = ot_null, cursor = ((ot_namespace_obj*)ot_as_obj(space))->vars, entry = ot_nil;
  OT_FRAME_SCOPED(s, &result, &cursor, &entry);
  while (ot_has_type(cursor, OBJ_VAR)) {
    entry = ot_table_new(s, 6);
    dev_field(s, entry, "name", ((ot_var_obj*)ot_as_obj(cursor))->name);
    otv value = ((ot_var_obj*)ot_as_obj(cursor))->value;
    if (ot_has_type(value, OBJ_MACRO)) dev_keyword_field(s, entry, "kind", "macro");
    else if (ot_has_type(value, OBJ_FUNCTION) || ot_has_type(value, OBJ_NAT))
      dev_keyword_field(s, entry, "kind", "function");
    else dev_keyword_field(s, entry, "kind", "value");
    value = ((ot_var_obj*)ot_as_obj(cursor))->value;
    if (ot_has_type(value, OBJ_FUNCTION)) {
      ot_code_obj* code = (ot_code_obj*)ot_as_obj(((ot_function_obj*)ot_as_obj(value))->code);
      dev_field(s, entry, "parameters", code->params);
    }
    dev_field(s, entry, "public",
              ((ot_var_obj*)ot_as_obj(cursor))->private_value ? ot_false : ot_true);
    const char* text;
    size_t length;
    ot_binding_documentation(cursor, &text, &length);
    char* doc = copy_string(text, length);
    dev_string_field(s, entry, "doc", doc);
    ot_host_free(doc);
    result = ot_cons(s, entry, result);
    cursor = ((ot_var_obj*)ot_as_obj(cursor))->next;
  }
  return result;
}
static otv dev_evaluate(ots* s, otv* args, int argc) {
  const char *source, *space, *path;
  size_t length, space_length, path_length;
  if (argc != 3 || !ot_string_bytes(args[0], &source, &length) ||
      !ot_string_bytes(args[1], &space, &space_length) ||
      !ot_string_bytes(args[2], &path, &path_length))
    return ot_raise(s, "eval: expected string code, namespace and path");
  char *code = copy_string(source, length), *ns = copy_string(space, space_length);
  char* filename = copy_string(path, path_length);
  otv saved_ns = s->vm.current_namespace, values = ot_nil, result = ot_nil;
  otv condition = ot_nil, response = ot_nil, output = ot_nil;
  OT_FRAME_SCOPED(s, &saved_ns, &values, &result, &condition, &response, &output);
  if (!dev_stdio) ot_select_namespace(s, ns, space_length);
  ot_host_free(ns);
  bool ok;
  bytes capture = {0};
  bytes* saved_capture = dev_capture;
  dev_capture = &capture;
  values = ot_array_new(s, 4);
  size_t line = 1;
  bool saved_hook = s->vm.in_interrupt_hook;
  if (!dev_stdio) s->vm.in_interrupt_hook = false;
  ok = ot_eval_results(s, code, length, filename, &values, &line);
  s->vm.in_interrupt_hook = saved_hook;
  dev_capture = saved_capture;
  ot_host_free(code);
  if (!dev_stdio) s->vm.current_namespace = saved_ns;
  if (!ok && (s->vm.unwind_kind == UNWIND_RESTART || s->quit_requested ||
              (dev_stdio && s->vm.unwind_kind == UNWIND_QUIT))) {
    ot_host_free(capture.data);
    ot_host_free(filename);
    return OT_UNWIND;
  }
  if (!ok) {
    condition = ot_condition(s);
    ot_clear_condition(s);
  }
  output = ot_make_string(s, capture.data ? capture.data : "", capture.length);
  ot_host_free(capture.data);
  response = ot_table_new(s, 6);
  dev_field(s, response, "ok", ok ? ot_true : ot_false);
  if (!ok && ot_has_type(condition, OBJ_TABLE)) {
    result = ot_table_new(s, 2);
    otv source_path = ot_make_string(s, filename, strlen(filename));
    OT_FRAME_SCOPED(s, &source_path);
    dev_field(s, result, "path", source_path);
    dev_field(s, result, "line", ot_make_int(line));
    dev_field(s, condition, "source", result);
  }
  ot_host_free(filename);
  dev_field(s, response, "condition", condition);
  dev_field(s, response, "output", output);
  dev_field(s, response, "values", values);
  result = ot_array_length(values) ? ot_array_get(values, ot_array_length(values) - 1, ot_nil)
                                   : ot_make_string(s, "nil", 3);
  dev_field(s, response, "value", result);
  return response;
}
static void dev_module(ots* s) {
  ot_def_nat(s, "%enabled?", dev_enabled_p);
  ot_def_nat(s, "%clock", dev_clock);
  ot_def_nat(s, "%pid", dev_pid);
  ot_def_nat(s, "%uid", dev_uid);
  ot_def_nat(s, "%private-directory!", dev_private_directory);
  ot_def_nat(s, "%open", dev_open);
  ot_def_nat(s, "%cwd", dev_cwd);
  ot_def_nat(s, "%runtime", dev_runtime);
  ot_def_nat(s, "%now", dev_now);
  ot_def_nat(s, "%mtime", dev_mtime);
  ot_def_nat(s, "%realpath", dev_realpath);
  ot_def_nat(s, "%namespaces", dev_namespaces);
  ot_def_nat(s, "%bindings", dev_bindings);
  ot_def_nat(s, "%poll", dev_poll);
  ot_def_nat(s, "%next", dev_next);
  ot_def_nat(s, "%client", dev_client);
  ot_def_nat(s, "%send", dev_send);
  ot_def_nat(s, "%repr", dev_render);
  ot_def_nat(s, "%deadline", dev_set_deadline);
  ot_def_nat(s, "%evaluate", dev_evaluate);
  otv ignored;
  ot_eval_src(s, ot_dev_src, ot_dev_src_len, "<otium.dev>", &ignored);
}
static bool dev_due(void* userdata) {
  (void)userdata;
  uint64_t now = ot_dev_clock();
  if (now - dev_last_poll < 25) return false;
  dev_last_poll = now;
  return dev_output.length || ot_dev_poll(0) || (dev_deadline && now >= dev_deadline);
}
static void dev_flush_output(ots* s) {
  if (!dev_output.length) return;
  otv text = ot_make_string(s, dev_output.data, dev_output.length);
  OT_FRAME_SCOPED(s, &text);
  dev_output.length = 0;
  bytes message = {0};
  bytes_append(&message, "{:event :output :text ", 21);
  ot_repr_to(s, text, false, dev_bytes_writer, &message);
  bytes_append(&message, "}\0", 2);
  ot_dev_send(0, message.data);
  ot_host_free(message.data);
}
static ot_interrupt_action dev_break(ots* s, void* userdata) {
  (void)userdata;
  otv saved = s->vm.current_namespace;
  OT_FRAME_SCOPED(s, &saved);
  bytes code = {0};
  const char* prefix = saw_interrupt ? "(otium.dev/break! #t " : "(otium.dev/break! #f ";
  bytes_append(&code, prefix, strlen(prefix));
  otv name = ((ot_namespace_obj*)ot_as_obj(saved))->name;
  bytes name_text = {0};
  ot_repr_to(s, name, false, dev_bytes_writer, &name_text);
  name = ot_make_string(s, name_text.data, name_text.length);
  ot_host_free(name_text.data);
  ot_repr_to(s, name, false, dev_bytes_writer, &code);
  bytes_append(&code, ")", 1);
  saw_interrupt = 0;
  dev_flush_output(s);
  otv result = ot_false;
  bool ok = ot_eval_src(s, code.data, code.length, "<dev-break>", &result);
  ot_host_free(code.data);
  s->vm.current_namespace = saved;
  return !ok || result == ot_true ? OT_INTERRUPT_ERROR : OT_INTERRUPT_CONTINUE;
}
static bool dev_start(ots* s, const char* path, size_t watchdog) {
  atexit(ot_dev_close);
  bytes code = {0};
  const char* prefix = "(require (otium.dev :as dev)) (otium.dev/listen! ";
  bytes_append(&code, prefix, strlen(prefix));
  otv text = ot_make_string(s, path, strlen(path));
  ot_repr_to(s, text, false, dev_bytes_writer, &code);
  char timeout[32];
  int count = snprintf(timeout, sizeof timeout, " %zu)", watchdog);
  bytes_append(&code, timeout, (size_t)count);
  otv ignored;
  bool ok = ot_eval_src(s, code.data, code.length, "<dev-start>", &ignored);
  ot_host_free(code.data);
  if (!ok) {
    print_value(s, ot_condition(s), stderr);
    return false;
  }
  dev_enabled = true;
  ot_set_writer(s, dev_writer, NULL);
  ot_set_interrupt_hook(s, dev_break, NULL);
  s->poll_hook = dev_due;
  return true;
}
static void dev_service(ots* s) {
  if (!dev_enabled) return;
  dev_flush_output(s);
  otv ignored;
  const char* code = "(otium.dev/service!)";
  if (!ot_eval_src(s, code, strlen(code), "<dev-service>", &ignored)) {
    print_value(s, ot_condition(s), stderr);
    ot_clear_condition(s);
  }
}

static void dev_idle(void* userdata) { dev_service(userdata); }

static bool dev_stdio_evaluate(ots* s, const char* source, size_t length, otv* result) {
  otv code = ot_make_string(s, source, length);
  bytes request = {0};
  const char* prefix = "(otium.dev/stdio! ";
  bytes_append(&request, prefix, strlen(prefix));
  ot_repr_to(s, code, false, dev_bytes_writer, &request);
  bytes_append(&request, ")", 1);
  dev_stdio = true;
  bool ok = ot_eval_src(s, request.data, request.length, "<server>", result);
  dev_stdio = false;
  ot_host_free(request.data);
  return ok;
}

static void finish_server_response(void) {
  fputs("\x1e"
        "ot> ",
        stdout);
  fflush(stdout);
}

static bool read_server_request(bytes* request) {
  char line[4096];
  while (fgets(line, sizeof line, stdin) != NULL) {
    size_t length = strlen(line);
    bool end =
        (length == 2 && line[0] == '\x1f' && line[1] == '\n') || (length == 1 && line[0] == '\x1f');
    if (!end) {
      bytes_append(request, line, length);
      continue;
    }
    return true;
  }
  return false;
}

static ot_interrupt_action run_server_break(ots* state, void* userdata) {
  (void)userdata;
  saw_interrupt = 0;
  fputs("paused\n", stdout);
  finish_server_response();

  bytes request = {0};
  while (read_server_request(&request)) {
    if (request.length != 0) {
      otv result;
      if (ot_eval_src(state, request.data, request.length, "<server>", &result)) {
        print_value(state, result, stdout);
      } else if (state->vm.unwind_kind == UNWIND_RESTART || state->vm.unwind_kind == UNWIND_QUIT) {
        ot_host_free(request.data);
        return OT_INTERRUPT_ABORT;
      } else {
        fputs("error: ", stdout);
        print_value(state, ot_condition(state), stdout);
        ot_clear_condition(state);
      }
    }
    request.length = 0;
    finish_server_response();
  }
  clearerr(stdin);
  ot_host_free(request.data);
  return OT_INTERRUPT_ABORT;
}

static void run_server(ots* state) {
  ot_set_writer(state, dev_writer, NULL);
  otv initialized;
  const char* initialize = "(require otium.dev)";
  if (!ot_eval_src(state, initialize, strlen(initialize), "<server>", &initialized)) return;
  bytes request = {0};
  ot_set_interrupt_hook(state, run_server_break, NULL);
  while (read_server_request(&request)) {
    bool stop = false;
    if (request.length != 0) {
      otv result;
      if (dev_stdio_evaluate(state, request.data, request.length, &result)) {
      } else if (state->vm.unwind_kind == UNWIND_QUIT) {
        stop = state->quit_requested;
        if (!stop) fputs("interrupted\n", stdout);
        ot_clear_condition(state);
      } else {
        fputs("error: ", stdout);
        print_value(state, ot_condition(state), stdout);
        ot_clear_condition(state);
      }
    }
    request.length = 0;
    if (stop) break;
    finish_server_response();
  }
  ot_set_interrupt_hook(state, NULL, NULL);
  ot_host_free(request.data);
}

static bool blank_line(const char* line) {
  for (; *line != '\0'; line++)
    if (!isspace((unsigned char)*line)) return false;
  return true;
}

static bool repl_evaluate(ots* state, bytes* pending) {
  size_t consumed;
  bool incomplete;
  otv result;
  if (ot_eval_partial(state, pending->data, pending->length, "<repl>", &consumed, &incomplete,
                      &result)) {
    print_value(state, result, stdout);
    pending->length = 0;
    return true;
  }
  if (consumed != 0) {
    memmove(pending->data, pending->data + consumed, pending->length - consumed);
    pending->length -= consumed;
  }
  if (incomplete) {
    ot_clear_condition(state);
    return true;
  }
  if (state->vm.unwind_kind == UNWIND_QUIT) {
    ot_clear_condition(state);
    return false;
  }
  if (saw_interrupt) {
    saw_interrupt = 0;
    fputs("interrupted\n", stdout);
  } else {
    fputs("error: ", stderr);
    print_value(state, ot_condition(state), stderr);
  }
  ot_clear_condition(state);
  pending->length = 0;
  return true;
}

/* Snapshot names and docs into host storage. None of these calls allocate in
 * the managed heap or run Otium code. Local definitions precede referred names. */
static void repl_binding(otv name_value, otv var_value, const char* prefix) {
  ot_name_obj* name = (ot_name_obj*)ot_as_obj(name_value);
  bytes qualified = {0};
  if (prefix != NULL) {
    bytes_append(&qualified, prefix, strlen(prefix));
    bytes_append(&qualified, "/", 1);
  }
  bytes_append(&qualified, name->bytes, name->length);
  bytes_append(&qualified, "\0", 1);
  const char* text = "";
  size_t length = 0;
  ot_binding_documentation(var_value, &text, &length);
  char* documentation = copy_string(text, length);
  ot_line_binding(qualified.data, documentation);
  ot_host_free(documentation);
  ot_host_free(qualified.data);
}

static void repl_namespace_bindings(otv namespace_value, const char* prefix, bool private_ok) {
  ot_namespace_obj* space = (ot_namespace_obj*)ot_as_obj(namespace_value);
  for (otv cursor = space->vars; ot_has_type(cursor, OBJ_VAR);) {
    ot_var_obj* var = (ot_var_obj*)ot_as_obj(cursor);
    if (private_ok || !var->private_value) repl_binding(var->name, cursor, prefix);
    cursor = var->next;
  }
}

static void repl_bindings(ots* state) {
  ot_line_bindings_clear();
  otv current = state->vm.current_namespace;
  ot_namespace_obj* space = (ot_namespace_obj*)ot_as_obj(current);
  repl_namespace_bindings(current, NULL, true);
  for (otv cursor = space->refers; ot_has_type(cursor, OBJ_ALIAS);) {
    ot_alias_obj* alias = (ot_alias_obj*)ot_as_obj(cursor);
    repl_binding(alias->name, alias->value, NULL);
    cursor = alias->next;
  }
  for (otv cursor = space->aliases; ot_has_type(cursor, OBJ_ALIAS);) {
    ot_alias_obj* alias = (ot_alias_obj*)ot_as_obj(cursor);
    for (otv target = state->namespaces; ot_has_type(target, OBJ_NAMESPACE);) {
      ot_namespace_obj* namespace_object = (ot_namespace_obj*)ot_as_obj(target);
      if (namespace_object->name == alias->value)
        repl_namespace_bindings(target, ((ot_name_obj*)ot_as_obj(alias->name))->bytes,
                                target == current);
      target = namespace_object->next;
    }
    cursor = alias->next;
  }
  for (otv target = state->namespaces; ot_has_type(target, OBJ_NAMESPACE);) {
    ot_namespace_obj* namespace_object = (ot_namespace_obj*)ot_as_obj(target);
    repl_namespace_bindings(target, ((ot_name_obj*)ot_as_obj(namespace_object->name))->bytes,
                            target == current);
    target = namespace_object->next;
  }
  for (otv cursor = state->symbols;
       ot_has_type(cursor, OBJ_SYMBOL) || ot_has_type(cursor, OBJ_KEYWORD);) {
    ot_name_obj* name = (ot_name_obj*)ot_as_obj(cursor);
    if (name->special_form) ot_line_binding(name->bytes, ot_runtime_documentation(name->bytes));
    cursor = name->next;
  }
}

static void run_repl(ots* state) {
  fputs("otium repl (c) - ^C interrupts, ^D exits, ^X ^E opens $EDITOR, Tab completes, ,help for "
        "help\n",
        stdout);
  bytes pending = {0};
  if (dev_enabled) ot_line_set_service(dev_idle, state);
  bool interactive = isatty(STDIN_FILENO);
  for (;;) {
    repl_bindings(state);
    const char* line = ot_line_read(pending.length == 0 ? "ot> " : "..> ");
    if (line == NULL) break;
    if (ot_line_interrupted()) {
      pending.length = 0;
      saw_interrupt = 0;
      continue;
    }
    if (pending.length == 0 && blank_line(line)) continue;
    if (pending.length != 0 && pending.data[pending.length - 1] != '\n')
      bytes_append(&pending, "\n", 1);
    bytes_append(&pending, line, strlen(line));
    if (!repl_evaluate(state, &pending)) break;
  }
  if (pending.length != 0) {
    size_t message_length;
    const char* message = condition_message(state, &message_length);
    (void)message;
    (void)message_length;
    ot_clear_condition(state);
  }
  ot_host_free(pending.data);
  ot_line_close();
  if (interactive) {
    fputc('\n', stdout);
  }
}

static bool parse_number(const char* option, const char* text, size_t* out) {
  for (const char* p = text; *p; p++) {
    if (*p < '0' || *p > '9') {
      fprintf(stderr, "otium: %s requires an integer\n", option);
      return false;
    }
  }
  errno = 0;
  char* end;
  unsigned long long value = strtoull(text, &end, 10);
  if (errno != 0 || end == text || *end != '\0' || value == 0 || value > SIZE_MAX) {
    fprintf(stderr, "otium: %s requires an integer\n", option);
    return false;
  }
  *out = (size_t)value;
  return true;
}

#include "options.inc"

static void usage(FILE* stream) { fputs(launcher_help, stream); }

static void print_byte_stat(const char* name, uint64_t bytes_value) {
  static const char* units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
  double value = (double)bytes_value;
  size_t unit = 0;
  while (value >= 1024 && unit + 1 < sizeof units / sizeof units[0]) {
    value /= 1024;
    unit++;
  }
  if (unit == 0)
    fprintf(stderr, "  %s: %" PRIu64 " (%" PRIu64 " B)\n", name, bytes_value, bytes_value);
  else fprintf(stderr, "  %s: %" PRIu64 " (%.2f %s)\n", name, bytes_value, value, units[unit]);
}

static void print_gc_stats(ots* state) {
  ot_gc_stats stats = ot_get_gc_stats(state);
  fputs("GC stats:\n", stderr);
#define OT_GC_PRINT_COUNT(NAME, VALUE)                                                             \
  fprintf(stderr, "  %s: %" PRIu64 "\n", NAME, (uint64_t)(VALUE))
#define OT_GC_PRINT_BYTES(NAME, VALUE) print_byte_stat(NAME, (uint64_t)(VALUE))
#define OT_GC_STAT(TYPE, MEMBER, JSON, HUMAN, UNIT) OT_GC_PRINT_##UNIT(HUMAN, stats.MEMBER);
#define OT_GC_PHASE(MEMBER, HUMAN, COLLECTIONS_JSON, TOTAL_JSON, MAX_JSON)                         \
  fprintf(stderr, "  %s: %" PRIu64 " (total %" PRIu64 " ns, max %" PRIu64 " ns)\n", HUMAN,         \
          stats.MEMBER.collections, stats.MEMBER.total_pause_ns, stats.MEMBER.max_pause_ns);
  OT_GC_STATS_FIELDS(OT_GC_STAT, OT_GC_PHASE)
#undef OT_GC_PHASE
#undef OT_GC_STAT
#undef OT_GC_PRINT_BYTES
#undef OT_GC_PRINT_COUNT
}

static void print_gc_stats_json(ots* state) {
  ot_gc_stats stats = ot_get_gc_stats(state);
  bool first = true;
  fputs("OTIUM_GC_STATS {", stderr);
#define OT_GC_JSON(NAME, VALUE)                                                                    \
  fprintf(stderr, "%s\"%s\":%" PRIu64, first ? "" : ",", NAME, (uint64_t)(VALUE));                 \
  first = false
#define OT_GC_STAT(TYPE, MEMBER, JSON, HUMAN, UNIT) OT_GC_JSON(JSON, stats.MEMBER);
#define OT_GC_PHASE(MEMBER, HUMAN, COLLECTIONS_JSON, TOTAL_JSON, MAX_JSON)                         \
  OT_GC_JSON(COLLECTIONS_JSON, stats.MEMBER.collections);                                          \
  OT_GC_JSON(TOTAL_JSON, stats.MEMBER.total_pause_ns);                                             \
  OT_GC_JSON(MAX_JSON, stats.MEMBER.max_pause_ns);
  OT_GC_STATS_FIELDS(OT_GC_STAT, OT_GC_PHASE)
#undef OT_GC_PHASE
#undef OT_GC_STAT
#undef OT_GC_JSON
  fputs("}\n", stderr);
}

static void add_environment_paths(void) {
  const char* value = getenv("OTIUM_PATH");
  if (value == NULL) return;
  const char* start = value;
  for (;;) {
    const char* colon = strchr(start, ':');
    size_t length = colon == NULL ? strlen(start) : (size_t)(colon - start);
    if (length != 0) strings_push(&load_path, start, length);
    if (colon == NULL) break;
    start = colon + 1;
  }
}

static void add_project_paths(const char* path) {
  char* source;
  size_t length;
  if (!ot_platform_read_file(path, &source, &length)) return;
  const char* slash = strrchr(path, '/');
  size_t directory_length = slash == NULL ? 0 : (size_t)(slash - path);
  for (size_t i = 0; i < length; i++) {
    if (source[i] != '"') continue;
    size_t start = ++i;
    while (i < length && source[i] != '"') i++;
    if (i == length) break;
    bytes resolved = {0};
    if (start == i || source[start] != '/') {
      if (directory_length != 0) bytes_append(&resolved, path, directory_length);
      else bytes_append(&resolved, ".", 1);
      bytes_append(&resolved, "/", 1);
    }
    bytes_append(&resolved, source + start, i - start);
    strings_push(&load_path, resolved.data, resolved.length);
    ot_host_free(resolved.data);
  }
  ot_host_free(source);
}

static char* find_project(void) {
  char directory[PATH_MAX];
  if (getcwd(directory, sizeof directory) == NULL) return NULL;
  for (;;) {
    bytes candidate = {0};
    bytes_append(&candidate, directory, strlen(directory));
    if (candidate.length > 1) bytes_append(&candidate, "/", 1);
    bytes_append(&candidate, "project.ot\0", 11);
    if (access(candidate.data, R_OK) == 0) return candidate.data;
    ot_host_free(candidate.data);
    char* slash = strrchr(directory, '/');
    if (slash == NULL || slash == directory) break;
    *slash = '\0';
  }
  return NULL;
}

int main(int argc, char** argv) {
  ot_config config = ot_config_default();
  strings files = {0};
  bool repl = false;
  bool server = false;
  bool serve = false;
  const char* dev_path = NULL;
  size_t watchdog = 2000;
  bool stats = false;
  bool stats_json = false;
  bool no_project = false;
  const char* project = NULL;
  const char* script = NULL;
  int application_start = argc;
  for (int i = 1; i < argc; i++) {
    const char* argument = argv[i];
    if (strcmp(argument, "--") == 0) {
      application_start = i + 1;
      if (script == NULL) {
        fputs("otium: -- requires a script\n", stderr);
        return 2;
      }
      break;
    }
    if (strcmp(argument, "serve") == 0 && script == NULL && !serve) {
      serve = true;
      if (!dev_path) dev_path = "";
      continue;
    }
    if (strcmp(argument, "--dev") == 0) {
      dev_path = "";
      continue;
    }
    if (strncmp(argument, "--dev=", 6) == 0) {
      dev_path = argument + 6;
      continue;
    }
    if (strcmp(argument, "--dev-watchdog") == 0) {
      fputs("otium: --dev-watchdog requires =MS\n", stderr);
      return 2;
    }
    if (strncmp(argument, "--dev-watchdog=", 15) == 0) {
      if (strcmp(argument + 15, "0") == 0) watchdog = 0;
      else if (!parse_number("dev-watchdog", argument + 15, &watchdog)) return 2;
      if (watchdog > 2147483647) {
        fputs("otium: watchdog out of range\n", stderr);
        return 2;
      }
      continue;
    }
    if (argument[0] != '-') {
      if (script != NULL) {
        fputs("otium: extra script; use --load or put application arguments after --\n", stderr);
        return 2;
      }
      script = argument;
      continue;
    }
    if (strcmp(argument, "-h") == 0) argument = "--help";
    if (strcmp(argument, "-L") == 0) argument = "--path";
    const char* equal = strchr(argument, '=');
    size_t name_length = equal ? (size_t)(equal - argument) : strlen(argument);
    size_t option;
    for (option = 0; option < sizeof launcher_options / sizeof *launcher_options; option++) {
      const char* name = launcher_options[option].name;
      if (name_length == strlen(name) + 2 && strncmp(argument, "--", 2) == 0 &&
          strncmp(argument + 2, name, name_length - 2) == 0)
        break;
    }
    if (option == sizeof launcher_options / sizeof *launcher_options) {
      fprintf(stderr, "otium: unknown option %s\n", argument);
      return 2;
    }
    const char* name = launcher_options[option].name;
    const char* kind = launcher_options[option].kind;
    const char* value = equal ? equal + 1 : NULL;
    if (strcmp(kind, "flag") == 0) {
      if (value) {
        fprintf(stderr, "otium: --%s takes no value\n", name);
        return 2;
      }
    } else if (strcmp(kind, "integer") == 0) {
      if (!value) {
        fprintf(stderr, "otium: --%s requires =INTEGER\n", name);
        return 2;
      }
    } else if (!value) {
      if (++i == argc) {
        fprintf(stderr, "otium: --%s requires a %s\n", name, kind);
        return 2;
      }
      value = argv[i];
    }
    size_t number = 0;
    if (strcmp(kind, "integer") == 0) {
      if (!parse_number(name, value, &number)) return 2;
      if ((strcmp(name, "c.heap-init") == 0 && number < 1024) ||
          (strcmp(name, "c.max-depth") == 0 && number > UINT_MAX)) {
        fprintf(stderr, "otium: --%s out of range\n", name);
        return 2;
      }
    }
    if (strcmp(name, "help") == 0) {
      usage(stdout);
      return 0;
    } else if (strcmp(name, "repl") == 0) repl = true;
    else if (strcmp(name, "c.server") == 0) server = true;
    else if (strcmp(name, "c.gc-stats") == 0) stats = true;
    else if (strcmp(name, "c.gc-stats-json") == 0) stats_json = true;
    else if (strcmp(name, "c.gc-force-compact") == 0) config.gc_force_compact = true;
    else if (strcmp(name, "no-project") == 0) no_project = true;
    else if (strcmp(name, "project") == 0) project = value;
    else if (strcmp(name, "path") == 0) strings_push(&load_path, value, strlen(value));
    else if (strcmp(name, "load") == 0) strings_push(&files, value, strlen(value));
    else if (strcmp(name, "c.heap-init") == 0) config.heap_init = number;
    else if (strcmp(name, "c.heap-max") == 0) config.heap_max = number;
    else if (strcmp(name, "c.max-depth") == 0) config.max_depth = (unsigned)number;
  }
  if (script) strings_push(&files, script, strlen(script));
  if (dev_path && server) {
    fputs("otium: --dev and --c.server cannot be used together\n", stderr);
    return 2;
  }
  if (repl && server) {
    fputs("otium: --repl and --c.server cannot be used together\n", stderr);
    return 2;
  }
  if (no_project && project != NULL) {
    fputs("otium: --project and --no-project cannot be used together\n", stderr);
    return 2;
  }
  if (config.heap_init < 1024) {
    fputs("otium: --c.heap-init must be at least 1024 bytes\n", stderr);
    return 2;
  }
  if (config.heap_max < config.heap_init) {
    fputs("otium: --c.heap-max must be at least --c.heap-init\n", stderr);
    return 2;
  }
  add_environment_paths();
  char* found_project = NULL;
  if (!no_project) {
    found_project = project == NULL ? find_project() : NULL;
    add_project_paths(project == NULL ? found_project : project);
  }
  ot_host_free(found_project);
  if (load_path.length == 0) strings_push(&load_path, ".", 1);
#ifdef OT_LIB_DIR
  // The standard library is searched after every user directory.
  strings_push(&load_path, OT_LIB_DIR, strlen(OT_LIB_DIR));
#endif

  ots* state = ot_create(&config);
  if (state == NULL) {
    fputs("otium: runtime creation failed\n", stderr);
    return 1;
  }
  signal_state = state;
  if (getenv("OTIUM_GC_STRESS") != NULL) state->config.gc_stress = true;
  ot_set_writer(state, stdout_writer, NULL);
  ot_set_loader(state, module_loader, state);
  ot_register_demo_extension(state);
  ot_register_module(state, "otium.dev", dev_module);
  ot_set_arguments(state, script, argc - application_start, argv + application_start);
  struct sigaction action = {0};
  action.sa_handler = handle_interrupt;
  sigaction(SIGINT, &action, NULL);

  int status = dev_path && !dev_start(state, dev_path, watchdog) ? 1 : 0;
  for (size_t i = 0; i < files.length && status == 0 && !state->quit_requested; i++)
    status = run_file(state, files.values[i]);
  if (status == 0 && !state->quit_requested) {
    if (serve) {
      while (!state->quit_requested) {
        ot_dev_poll(50);
        dev_service(state);
      }
    } else if (server) run_server(state);
    else if (repl || files.length == 0) run_repl(state);
  }
  if (stats) print_gc_stats(state);
  if (stats_json) print_gc_stats_json(state);
  if (state->quit_requested) status = state->quit_status;
  ot_dev_close();
  ot_host_free(dev_output.data);
  ot_destroy(state);
  signal_state = NULL;
  for (size_t i = 0; i < files.length; i++) ot_host_free(files.values[i]);
  ot_host_free(files.values);
  for (size_t i = 0; i < load_path.length; i++) ot_host_free(load_path.values[i]);
  ot_host_free(load_path.values);
  return status;
}
