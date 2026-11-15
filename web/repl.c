/* The browser REPL's entry points. The page keeps one runtime alive in a
 * worker and hands it each entry with otium_eval, so definitions persist
 * between entries the way they do in the terminal REPL. Output goes through
 * stdout and stderr, which the worker collects and posts back. */
#include <emscripten/emscripten.h>
#include <stdio.h>
#include <string.h>

#include "otium.h"

#ifndef OTIUM_LIBRARY_DIR
#define OTIUM_LIBRARY_DIR "/otium/lib"
#endif

enum { REPL_DONE, REPL_INCOMPLETE, REPL_FAILED, REPL_EXITED };

static ot_state* rt;

static void flush(void) {
  if (!ot_flush_ports(rt)) fprintf(stderr, "cannot flush output ports: %s\n", ot_error(rt));
  fflush(stdout);
  fflush(stderr);
}

/* Create the runtime and load core. False after printing why. */
EMSCRIPTEN_KEEPALIVE bool otium_start(void) {
  /* The terminal REPL's defaults: 1 MiB to start, up to 1 GiB. */
  rt = ot_create(1024 * 1024, 128 * 1024);
  if (!rt || !ot_set_heap_limit(rt, 1024 * 1024 * 1024)) {
    fprintf(stderr, "cannot initialize runtime%s%s\n", rt ? ": " : "", rt ? ot_error(rt) : "");
    return false;
  }
  static const char* const no_arguments[] = {NULL};
  bool ok = ot_set_arguments(rt, NULL, 0, no_arguments) && ot_load_expander(rt, OTIUM_LIBRARY_DIR);
  if (!ok) fprintf(stderr, "cannot load expander: %s\n", ot_error(rt));
  else if (!(ok = ot_load_core(rt, OTIUM_LIBRARY_DIR)))
    fprintf(stderr, "cannot load core: %s\n", ot_error(rt));
  flush();
  return ok;
}

/* Evaluate every form in source, printing each result. Nothing runs when the
 * last form is unfinished: the page asks for more lines and sends the whole
 * entry again. A syntax error is reported before anything runs. */
EMSCRIPTEN_KEEPALIVE int otium_eval(const char* source) {
  size_t length = strlen(source), end, error_offset;
  const char* error;
  for (size_t at = 0;; at = end) {
    ot_form_status status = ot_scan_form(source, length, at, &end, &error, &error_offset);
    if (status == OT_FORM_END) break;
    if (status == OT_FORM_INCOMPLETE) return REPL_INCOMPLETE;
    if (status == OT_FORM_ERROR) {
      fprintf(stderr, "<repl>:%zu: %s\n", error_offset, error);
      fflush(stderr);
      return REPL_FAILED;
    }
  }
  int result = REPL_DONE;
  for (size_t at = 0; result == REPL_DONE; at = end) {
    if (ot_scan_form(source, length, at, &end, &error, &error_offset) != OT_FORM_COMPLETE) break;
    ot_value value;
    int exit_status;
    ot_set_source_path(rt, "<repl>");
    if (ot_eval(rt, source + at, end - at, &value)) {
      ot_print(rt, stdout, value);
      fputc('\n', stdout);
    } else if (ot_exit_status(rt, &exit_status)) {
      result = REPL_EXITED;
    } else {
      ot_report_error(rt, "<repl>", stderr);
      result = REPL_FAILED;
    }
  }
  flush();
  return result;
}
