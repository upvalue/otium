#define OT_INTERNAL
#include "otium.h"
static const char ot_cli_src[] = {
#embed "../../lib/otium/cli.ot"
};
static const unsigned ot_cli_src_len = sizeof ot_cli_src;
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void ot_set_arguments(ots* s, const char* script, int argc, char* const* argv) {
  s->process_arguments = ot_null;
  s->process_script = ot_false;
  otv value = ot_nil;
  OT_FRAME_SCOPED(s, &value);
  for (int i = argc; i > 0; i--) {
    value = ot_make_string(s, argv[i - 1], strlen(argv[i - 1]));
    s->process_arguments = ot_cons(s, value, s->process_arguments);
  }
  if (script) s->process_script = ot_make_string(s, script, strlen(script));
}

static otv arguments(ots* s, otv* args, int argc) {
  (void)args;
  return argc ? ot_raise(s, "arguments: expected no arguments") : s->process_arguments;
}

static otv script_name(ots* s, otv* args, int argc) {
  (void)args;
  return argc ? ot_raise(s, "script-name: expected no arguments") : s->process_script;
}

static otv stderr_write(ots* s, otv* args, int argc) {
  const char* bytes;
  size_t length;
  if (argc != 1 || !ot_string_bytes(args[0], &bytes, &length))
    return ot_raise(s, "write-error: expected string");
  fwrite(bytes, 1, length, stderr);
  return ot_nil;
}

// Copy an environment name or value to host memory. Names must be nonempty and
// free of '='; neither may contain NUL. Returns NULL for an invalid string.
static char* env_string(otv value, bool name) {
  const char* bytes;
  size_t length;
  if (!ot_string_bytes(value, &bytes, &length) || memchr(bytes, 0, length)) return NULL;
  if (name && (length == 0 || memchr(bytes, '=', length))) return NULL;
  char* copy = ot_host_alloc(length + 1);
  if (copy == NULL) abort();
  memcpy(copy, bytes, length);
  return copy;
}

static otv env_get(ots* s, otv* args, int argc) {
  char* name = argc == 1 ? env_string(args[0], true) : NULL;
  if (name == NULL) return ot_raise(s, "getenv: expected a variable name");
  const char* value = getenv(name);
  ot_host_free(name);
  return value ? ot_make_string(s, value, strlen(value)) : ot_nil;
}

static otv env_set(ots* s, otv* args, int argc) {
  char* name = argc == 2 ? env_string(args[0], true) : NULL;
  if (name == NULL) return ot_raise(s, "setenv!: expected a variable name");
  int result;
  if (args[1] == ot_nil) {
    result = unsetenv(name);
  } else {
    char* value = env_string(args[1], false);
    if (value == NULL) {
      ot_host_free(name);
      return ot_raise(s, "setenv!: expected a string without NUL or nil");
    }
    result = setenv(name, value, 1);
    ot_host_free(value);
  }
  ot_host_free(name);
  return result == 0 ? ot_nil : ot_raise(s, "setenv!: could not change the environment");
}

static void process_module(ots* s) {
  ot_def_nat(s, "arguments", arguments);
  ot_def_nat(s, "script-name", script_name);
  ot_def_nat(s, "write-error", stderr_write);
  ot_def_nat(s, "getenv", env_get);
  ot_def_nat(s, "setenv!", env_set);
}

static void cli_module(ots* s) {
  otv ignored;
  ot_eval_src(s, ot_cli_src, ot_cli_src_len, "<otium.cli>", &ignored);
}

void ot_register_cli(ots* s) {
  s->process_arguments = ot_null;
  s->process_script = ot_false;
  ot_global_add(s, &s->process_arguments);
  ot_global_add(s, &s->process_script);
  ot_register_module(s, "otium.process", process_module);
  ot_register_module(s, "otium.cli", cli_module);
}
