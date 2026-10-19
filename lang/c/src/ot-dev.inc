#include "dev/transport.h"
#include "dev.h"
#include <sys/stat.h>

static bool dev_enabled;
static bool dev_stdio;
static uint64_t dev_deadline, dev_last_poll;
static bytes* dev_capture;
static bytes dev_output;

static void dev_bytes_writer(void* userdata, const char* text, size_t length) {
  bytes_append(userdata, text, length);
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
  if (ot_dev_open(path) != 0) {
    fprintf(stderr, "otium: cannot listen on %s\n", path);
    return false;
  }
  dev_enabled = true;
  atexit(ot_dev_close);
  otv ignored;
  char code[160];
  snprintf(code, sizeof code, "(require otium.dev) (set! otium.dev/watchdog %zu)", watchdog);
  if (!ot_eval_src(s, code, strlen(code), "<dev-start>", &ignored)) {
    print_value(s, ot_condition(s), stderr);
    return false;
  }
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
