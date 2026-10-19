#define OT_INTERNAL
#include "otium.h"
#include "ot-cli-source.h"
#include <stdio.h>
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
static void process_module(ots* s) {
  ot_def_nat(s, "arguments", arguments);
  ot_def_nat(s, "script-name", script_name);
  ot_def_nat(s, "write-error", stderr_write);
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
