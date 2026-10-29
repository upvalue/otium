#define _POSIX_C_SOURCE 200809L
/* sigaltstack and SA_ONSTACK. */
#define _XOPEN_SOURCE 700
#include "otium.h"
#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
/* Angle brackets keep the shared native code out of the amalgamation. */
#include <line/line-edit.h>
#ifndef OTIUM_LIBRARY_DIR
#define OTIUM_LIBRARY_DIR "lib"
#endif
#ifndef OTIUM_DEV_FILE
#define OTIUM_DEV_FILE "c/src/dev.ot"
#endif

/* launcher_options and launcher_help, from lib/catalog/options.json. */
#include "options.inc"

/* 1 for SIGINT and 2 for the development server's timer tick, which the
 * interrupt handler in c/src/dev.ot tells apart. */
static volatile sig_atomic_t interrupt_requested;
/* The runtime the handlers interrupt, once it exists. */
static ot_state* volatile interrupt_runtime;

static void request(int reason) {
  if (interrupt_runtime) ot_request_interrupt(interrupt_runtime, reason);
  else interrupt_requested = reason;
}

static void interrupt_handler(int signal_number) {
  (void)signal_number;
  request(1);
}

/* Native code runs on small stacks with no room for a signal frame, so the
 * handlers here run on a stack of their own. */
static bool signal_stack(void) {
  static _Alignas(16) char memory[64 * 1024];
  static bool done;
  if (done) return true;
  stack_t stack = {.ss_sp = memory, .ss_size = sizeof(memory)};
  if (sigaltstack(&stack, NULL)) return false;
  done = true;
  return true;
}

static void tick_handler(int signal_number) {
  (void)signal_number;
  if (!interrupt_requested) request(2);
}

#define SOURCE_LIMIT (16u * 1024u * 1024u)
#define TICK_MICROSECONDS 10000

typedef struct {
  const char *script, *source, *project, *dev_path;
  size_t heap_init, heap_max, max_depth;
  unsigned watchdog;
  bool heap_max_given, repl, no_project, preloads, serve, server, gc_stats, gc_stats_json, vm,
      unlock_core;
  /* Launcher options end at option_end; program arguments start at arguments. */
  int option_end, arguments;
} options;

static int usage_error(const char* format, ...) {
  va_list args;
  va_start(args, format);
  fputs("otium: ", stderr);
  vfprintf(stderr, format, args);
  fputc('\n', stderr);
  va_end(args);
  return 2;
}

/* The one place the launcher prints a runtime error. */
static void report_error(ot_state* rt, const char* where) { ot_report_error(rt, where, stderr); }

static bool evaluate(ot_state* rt, const char* source, size_t length, const char* path, bool echo) {
  ot_value result;
  ot_set_source_path(rt, path);
  if (!ot_eval(rt, source, length, &result)) {
    int status;
    if (!ot_interrupted(rt) && !ot_exit_status(rt, &status)) report_error(rt, path);
    return false;
  }
  if (echo) {
    ot_print(rt, stdout, result);
    fputc('\n', stdout);
    fflush(stdout);
  }
  return true;
}

static char* read_source(const char* path, size_t* size) {
  FILE* file = fopen(path, "rb");
  if (!file) {
    perror(path);
    return NULL;
  }
  size_t capacity = 4096, length = 0;
  char* buffer = malloc(capacity);
  bool ok = buffer != NULL;
  if (!ok) fputs("out of host memory\n", stderr);
  while (ok) {
    size_t got = fread(buffer + length, 1, capacity - length, file);
    length += got;
    if (ferror(file)) {
      perror(path);
      ok = false;
      break;
    }
    if (feof(file)) break;
    if (capacity == SOURCE_LIMIT) {
      int next = fgetc(file);
      if (next != EOF || ferror(file)) {
        fprintf(stderr, "%s: source exceeds size limit or cannot be read\n", path);
        ok = false;
      }
      break;
    }
    capacity *= 2;
    char* grown = realloc(buffer, capacity);
    if (!grown) {
      fputs("out of host memory\n", stderr);
      ok = false;
      break;
    }
    buffer = grown;
  }
  fclose(file);
  if (!ok) {
    free(buffer);
    return NULL;
  }
  *size = length;
  return buffer;
}

static bool load(ot_state* rt, const char* path) {
  size_t length;
  char* buffer = read_source(path, &length);
  if (!buffer) return false;
  bool ok = evaluate(rt, buffer, length, path, false);
  free(buffer);
  return ok;
}

static bool discover_project(ot_state* rt) {
  char* directory = getcwd(NULL, 0);
  if (!directory) {
    perror("cannot find current directory");
    return false;
  }
  char* path = malloc(strlen(directory) + sizeof("/project.ot"));
  bool ok = path != NULL;
  if (!ok) fputs("out of host memory\n", stderr);
  while (ok) {
    sprintf(path, "%s/project.ot", directory);
    if (access(path, F_OK) == 0) {
      ok = ot_add_project_paths(rt, path);
      break;
    }
    if (errno != ENOENT && errno != ENOTDIR) {
      perror(path);
      ok = false;
      break;
    }
    char* slash = strrchr(directory, '/');
    if (!slash || !strcmp(directory, "/")) break;
    if (slash == directory) directory[1] = 0;
    else *slash = 0;
  }
  free(path);
  free(directory);
  return ok;
}

static bool environment_paths(ot_state* rt) {
  const char* value = getenv("OTIUM_PATH");
  if (!value) return true;
  char* paths = strdup(value);
  if (!paths) {
    fputs("out of host memory\n", stderr);
    return false;
  }
  bool ok = true;
  char* cursor = paths;
  while (ok && cursor) {
    char* next = strchr(cursor, ':');
    if (next) *next++ = 0;
    if (*cursor) ok = ot_add_load_path(rt, cursor);
    cursor = next;
  }
  free(paths);
  return ok;
}

/* Parse a positive decimal integer of at most limit. */
static bool integer_value(const char* text, unsigned long long limit, unsigned long long* out) {
  if (!*text) return false;
  for (const char* p = text; *p; p++)
    if (!isdigit((unsigned char)*p)) return false;
  errno = 0;
  unsigned long long value = strtoull(text, NULL, 10);
  if (errno || !value || value > limit) return false;
  *out = value;
  return true;
}

/* Fill o from argv. Returns 0 to continue, or an exit status. */
static int parse_options(int argc, char** argv, options* o) {
  *o = (options){.heap_init = 1024 * 1024,
                 .heap_max = 1024 * 1024 * 1024,
                 .watchdog = 2000,
                 .option_end = argc,
                 .arguments = argc};
  for (int i = 1; i < argc; i++) {
    const char* token = argv[i];
    if (!strcmp(token, "--")) {
      o->option_end = i;
      if (!o->script && !o->source) {
        if (i + 1 == argc) return usage_error("-- requires a script");
        o->script = argv[++i];
      }
      o->arguments = i + 1;
      return 0;
    }
    if (!strcmp(token, "serve") && !o->script && !o->source && !o->serve) {
      o->serve = true;
      if (!o->dev_path) o->dev_path = "";
      continue;
    }
    if (!strcmp(token, "-e")) {
      if (i + 1 == argc) return usage_error("-e requires source");
      if (o->script || o->source)
        return usage_error("extra script; use --load or put application arguments after --");
      o->source = argv[++i];
      continue;
    }
    if (token[0] != '-' || !token[1]) {
      if (o->script || o->source)
        return usage_error("extra script; use --load or put application arguments after --");
      o->script = token;
      continue;
    }
    if (!strcmp(token, "-h")) token = "--help";
    else if (!strcmp(token, "-L")) token = "--path";
    const char* equals = strchr(token, '=');
    size_t spelling = equals ? (size_t)(equals - token) : strlen(token);
    const char *name = NULL, *kind = NULL;
    for (size_t k = 0; k < sizeof(launcher_options) / sizeof(*launcher_options); k++)
      if (spelling == strlen(launcher_options[k].name) + 2 && !strncmp(token, "--", 2) &&
          !strncmp(token + 2, launcher_options[k].name, spelling - 2)) {
        name = launcher_options[k].name;
        kind = launcher_options[k].kind;
      }
    if (!name) return usage_error("unknown option %s", argv[i]);
    const char* value = equals ? equals + 1 : NULL;
    unsigned long long number = 0;
    if (!strcmp(kind, "optional-path")) {
      if (!value) value = "";
    } else if (!strcmp(kind, "milliseconds")) {
      if (!value || (strcmp(value, "0") && !integer_value(value, 2147483647, &number)))
        return usage_error("--%s requires =MS (nonnegative integer)", name);
    } else if (!strcmp(kind, "flag")) {
      if (value) return usage_error("--%s takes no value", name);
    } else if (!strcmp(kind, "integer")) {
      if (!value) return usage_error("--%s requires =INTEGER", name);
      if (!integer_value(value, 18446744073709551615ull, &number))
        return usage_error("--%s requires an integer", name);
    } else if (!value) {
      if (i + 1 == argc) return usage_error("--%s requires a %s", name, kind);
      value = argv[++i];
    }
    if (!strcmp(name, "help")) {
      fputs(launcher_help, stdout);
      return -1;
    } else if (!strcmp(name, "repl")) o->repl = true;
    else if (!strcmp(name, "project")) o->project = value;
    else if (!strcmp(name, "no-project")) o->no_project = true;
    else if (!strcmp(name, "load")) o->preloads = true;
    else if (!strcmp(name, "dev")) o->dev_path = value;
    else if (!strcmp(name, "dev-watchdog")) o->watchdog = (unsigned)number;
    else if (!strcmp(name, "c.server")) o->server = true;
    else if (!strcmp(name, "c.gc-stats")) o->gc_stats = true;
    else if (!strcmp(name, "c.gc-stats-json")) o->gc_stats_json = true;
    else if (!strcmp(name, "c.vm")) o->vm = true;
    else if (!strcmp(name, "c.unlock-core")) o->unlock_core = true;
    else if (!strcmp(name, "c.heap-init") || !strcmp(name, "c.heap-max")) {
      if (number > SIZE_MAX / 2 || (name[7] == 'i' && number < 1024))
        return usage_error("--%s out of range", name);
      if (name[7] == 'i') o->heap_init = (size_t)number;
      else {
        o->heap_max = (size_t)number;
        o->heap_max_given = true;
      }
    } else if (!strcmp(name, "c.max-depth")) {
      if (number > 4294967295ull) return usage_error("--c.max-depth out of range");
      o->max_depth = (size_t)number;
    }
  }
  return 0;
}

static int check_options(options* o) {
  if (o->project && o->no_project)
    return usage_error("--project and --no-project cannot be used together");
  if (o->server && (o->repl || o->serve || o->dev_path))
    return usage_error("--%s and --c.server cannot be used together", o->repl    ? "repl"
                                                                      : o->serve ? "serve"
                                                                                 : "dev");
  if (!o->heap_max_given && o->heap_max < o->heap_init) o->heap_max = o->heap_init;
  if (o->heap_max < o->heap_init) return usage_error("--c.heap-max must be at least --c.heap-init");
  if (!o->script && !o->source && !o->preloads && !o->serve && !o->server) o->repl = true;
  return 0;
}

/* Load paths: -L/--path first, then OTIUM_PATH, then the project's. */
static bool add_paths(ot_state* rt, const options* o, char** argv) {
  for (int i = 1; i < o->option_end; i++) {
    const char* path = NULL;
    if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "--load") || !strcmp(argv[i], "--project")) i++;
    else if (!strcmp(argv[i], "-L") || !strcmp(argv[i], "--path")) path = argv[++i];
    else if (!strncmp(argv[i], "--path=", 7)) path = argv[i] + 7;
    if (path && !ot_add_load_path(rt, path)) return false;
  }
  if (!environment_paths(rt)) return false;
  if (o->no_project) return true;
  return o->project ? ot_add_project_paths(rt, o->project) : discover_project(rt);
}

static bool preload(ot_state* rt, const options* o, char** argv) {
  for (int i = 1; i < o->option_end; i++) {
    if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "-L") || !strcmp(argv[i], "--path") ||
        !strcmp(argv[i], "--project"))
      i++;
    else if (!strcmp(argv[i], "--load") && !load(rt, argv[++i])) return false;
    else if (!strncmp(argv[i], "--load=", 7) && !load(rt, argv[i] + 7)) return false;
  }
  return true;
}

/* Load the server's Otium side: lib/otium/dev.ot, then the C runtime's part. */
static bool load_dev(ot_state* rt) {
  static const char require[] = "(require (otium.dev :as dev))";
  ot_value result;
  if (!ot_eval(rt, require, sizeof(require) - 1, &result) || !load(rt, OTIUM_DEV_FILE))
    return false;
  return ot_eval(rt, "(ns user)", 9, &result);
}

static bool start_dev(ot_state* rt, const options* o, bool native) {
  /* The path is passed as Otium source, so write it as a string literal. */
  size_t length = strlen(o->dev_path);
  char* source = malloc(length * 2 + 64);
  if (!source) return false;
  char* p = source + sprintf(source, "(otium.dev/%%start! \"");
  for (const char* c = o->dev_path; *c; c++) {
    if (*c == '"' || *c == '\\') *p++ = '\\';
    *p++ = *c;
  }
  sprintf(p, "\" %u %s)", o->watchdog, native ? "#t" : "#f");
  ot_value result;
  bool ok = ot_eval(rt, source, strlen(source), &result);
  free(source);
  if (!ok) return false;
  if (!ot_set_interrupt_handler(rt, "otium.dev/%interrupt") ||
      !ot_set_load_hook(rt, "otium.dev/record-load!"))
    return false;
  if (native) return true;
  /* The VM keeps the legacy service hook. The tick counts process CPU time,
   * so it never interrupts a blocking call and costs nothing while waiting. */
  struct sigaction action = {0};
  action.sa_handler = tick_handler;
  action.sa_flags = SA_RESTART | SA_ONSTACK;
  if (!signal_stack()) return false;
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGINT);
  struct itimerval timer = {{0, TICK_MICROSECONDS}, {0, TICK_MICROSECONDS}};
  return sigaction(SIGVTALRM, &action, NULL) == 0 && setitimer(ITIMER_VIRTUAL, &timer, NULL) == 0;
}

static void stop_dev(ot_state* rt) {
  struct itimerval off = {{0, 0}, {0, 0}};
  setitimer(ITIMER_VIRTUAL, &off, NULL);
  if (interrupt_requested == 2) interrupt_requested = 0;
  ot_value result, cancel = ot_bool(ot_interrupted(rt));
  ot_call(rt, "otium.dev/%stop!", &cancel, 1, &result);
  ot_set_interrupt_handler(rt, NULL);
  ot_set_load_hook(rt, NULL);
}

static void dev_service(void* data) {
  ot_state* rt = data;
  ot_value result;
  if (ot_call(rt, "otium.dev/service!", NULL, 0, &result)) return;
  int status;
  if (!ot_exit_status(rt, &status)) {
    if (!ot_interrupted(rt)) report_error(rt, "<dev>");
    return;
  }
  /* A client asked the program to exit while the REPL waited for input. */
  stop_dev(rt);
  ot_flush_ports(rt);
  exit(status);
}

/* Lines from the standard input port, which the REPL shares with programs.
 * Returns the length including a newline, -1 at EOF or -2 after reporting an
 * error. */
static ssize_t port_line(ot_state* rt, char** line, size_t* capacity) {
  ot_value port, value;
  if (!ot_call(rt, "otium.core/%standard-input", NULL, 0, &port) ||
      !ot_call(rt, "otium.core/read-line", &port, 1, &value)) {
    if (!ot_interrupted(rt)) fprintf(stderr, "stdin: %s\n", ot_error(rt));
    return -2;
  }
  if (value == OT_EOF) return -1;
  size_t length;
  const char* text = ot_string_data(value, &length);
  if (!text) {
    fputs("stdin: read-line must return a string or EOF\n", stderr);
    return -2;
  }
  if (length >= SOURCE_LIMIT) {
    fputs("stdin: source exceeds size limit\n", stderr);
    return -2;
  }
  if (*capacity < length + 1) {
    char* grown = realloc(*line, length + 1);
    if (!grown) {
      fputs("out of host memory\n", stderr);
      return -2;
    }
    *line = grown;
    *capacity = length + 1;
  }
  memcpy(*line, text, length);
  (*line)[length] = '\n';
  return (ssize_t)(length + 1);
}

/* Lines from the line editor, or -3 when ^C discarded the input. */
static ssize_t edited_line(const char* prompt, char** line, size_t* capacity) {
  const char* text = ot_line_read(prompt);
  if (!text) return -1;
  if (ot_line_interrupted()) return -3;
  size_t length = strlen(text);
  if (length >= SOURCE_LIMIT) {
    fputs("stdin: source exceeds size limit\n", stderr);
    return -2;
  }
  if (*capacity < length + 1) {
    char* grown = realloc(*line, length + 1);
    if (!grown) {
      fputs("out of host memory\n", stderr);
      return -2;
    }
    *line = grown;
    *capacity = length + 1;
  }
  memcpy(*line, text, length);
  (*line)[length] = '\n';
  return (ssize_t)(length + 1);
}

/* The line editor is used on a terminal, and with the development server so
 * requests are served while the REPL waits. Otherwise lines come from the
 * standard input port and evaluation stops at an interrupt. */
static bool repl(ot_state* rt, bool dev, bool native) {
  bool terminal = ot_line_interactive(), editing = (terminal || dev) && !(dev && native), ok = true;
  if (dev && !native) ot_line_set_service(dev_service, rt);
#if defined(__aarch64__)
  const char* machine = "native arm64";
#elif defined(__x86_64__)
  const char* machine = "native x86-64";
#else
  const char* machine = "native";
#endif
  if (terminal)
    printf("otium repl (%s) - ^C interrupts, ^D exits, ,help for help\n", native ? machine : "vm");
  char *line = NULL, *pending = NULL;
  size_t line_capacity = 0, length = 0;
  for (;;) {
    ssize_t bytes = editing ? edited_line(length ? "..> " : "> ", &line, &line_capacity)
                            : port_line(rt, &line, &line_capacity);
    if (bytes == -3) {
      length = 0;
      continue;
    }
    if (bytes < 0) {
      if (bytes == -2) ok = false;
      if (length) {
        size_t end, error_offset;
        const char* error;
        ot_scan_form(pending, length, 0, &end, &error, &error_offset);
        fprintf(stderr, "<stdin>:%zu: %s\n", error_offset, error ? error : "incomplete input");
        ok = false;
      }
      break;
    }
    if ((size_t)bytes > SOURCE_LIMIT - length) {
      fputs("<stdin>: source exceeds size limit\n", stderr);
      ok = false;
      break;
    }
    char* grown = realloc(pending, length + (size_t)bytes);
    if (!grown) {
      fputs("out of host memory\n", stderr);
      ok = false;
      break;
    }
    pending = grown;
    memcpy(pending + length, line, (size_t)bytes);
    length += (size_t)bytes;
    size_t consumed = 0;
    for (;;) {
      size_t end, error_offset;
      const char* error;
      ot_form_status status = ot_scan_form(pending, length, consumed, &end, &error, &error_offset);
      if (status == OT_FORM_COMPLETE) {
        evaluate(rt, pending + consumed, end - consumed, "<stdin>", true);
        int exit_status;
        if (ot_exit_status(rt, &exit_status) || (ot_interrupted(rt) && !editing)) {
          free(line);
          free(pending);
          if (dev) ot_line_set_service(NULL, NULL);
          return false;
        }
        if (ot_interrupted(rt)) fputs("interrupted\n", stderr);
        consumed = end;
        continue;
      }
      if (status == OT_FORM_END) consumed = length;
      else if (status == OT_FORM_ERROR) {
        fprintf(stderr, "<stdin>:%zu: %s\n", error_offset, error);
        consumed = length;
      }
      break;
    }
    memmove(pending, pending + consumed, length - consumed);
    length -= consumed;
    fflush(stdout);
  }
  free(line);
  free(pending);
  if (editing) ot_line_close();
  if (dev) ot_line_set_service(NULL, NULL);
  return ok;
}

/* The framed stdio server. An interrupted frame is answered with
 * "interrupted" and the server reads the next one. */
static bool stdio_server(ot_state* rt) {
  if (!ot_set_interrupt_handler(rt, "otium.dev/%stdio-interrupt")) return false;
  ot_value result;
  int status;
  bool ok;
  while (!(ok = ot_call(rt, "otium.dev/%stdio-run", NULL, 0, &result)) &&
         !ot_exit_status(rt, &status)) {
    if (!ot_interrupted(rt)) break;
    fputs("interrupted\n\x1eot> ", stdout);
    fflush(stdout);
  }
  ot_set_interrupt_handler(rt, NULL);
  if (!ok && !ot_interrupted(rt) && !ot_exit_status(rt, &status)) report_error(rt, "<server>");
  return ok;
}

static void print_size(const char* name, size_t bytes) {
  static const char* const units[] = {"B", "KiB", "MiB", "GiB", "TiB", "PiB", "EiB"};
  double value = (double)bytes;
  unsigned unit = 0;
  while (value >= 1024 && unit < 6) {
    value /= 1024;
    unit++;
  }
  if (unit) fprintf(stderr, "  %s: %zu (%.2f %s)\n", name, bytes, value, units[unit]);
  else fprintf(stderr, "  %s: %zu (%zu B)\n", name, bytes, bytes);
}

static void print_gc_stats(ot_state* rt, const options* o) {
  ot_stats s = ot_get_stats(rt);
  if (o->gc_stats) {
    fprintf(stderr, "GC stats:\n  collections: %zu\n", s.collections);
    print_size("allocated bytes", s.heap_allocated_bytes);
    print_size("copied bytes", s.heap_copied_bytes);
    print_size("reclaimed bytes", s.heap_reclaimed_bytes);
    print_size("heap used bytes", s.heap_used);
    print_size("peak heap used bytes", s.heap_peak_used);
    print_size("heap capacity bytes", s.heap_capacity);
    print_size("heap limit bytes", s.heap_limit);
    fprintf(stderr, "  collection time: %zu.%03zu ms\n  longest collection: %zu.%03zu ms\n",
            s.gc_microseconds / 1000, s.gc_microseconds % 1000, s.gc_longest_microseconds / 1000,
            s.gc_longest_microseconds % 1000);
  }
  if (o->gc_stats_json)
    fprintf(stderr,
            "OTIUM_GC_STATS {\"collections\":%zu,\"allocated_bytes\":%zu,"
            "\"copied_bytes\":%zu,\"reclaimed_bytes\":%zu,\"used_bytes\":%zu,"
            "\"peak_used_bytes\":%zu,\"capacity_bytes\":%zu,\"limit_bytes\":%zu,"
            "\"gc_microseconds\":%zu,\"gc_longest_microseconds\":%zu}\n",
            s.collections, s.heap_allocated_bytes, s.heap_copied_bytes,
            s.heap_reclaimed_bytes, s.heap_used, s.heap_peak_used, s.heap_capacity, s.heap_limit,
            s.gc_microseconds, s.gc_longest_microseconds);
}

int main(int argc, char** argv) {
  options o;
  int status = parse_options(argc, argv, &o);
  if (status) return status < 0 ? 0 : status;
  if ((status = check_options(&o))) return status;
  ot_state* rt = ot_create(o.heap_init, 128 * 1024);
  if (!rt) {
    fputs("cannot initialize runtime\n", stderr);
    return 1;
  }
  if (!ot_set_heap_limit(rt, o.heap_max) || (o.max_depth && !ot_set_frame_limit(rt, o.max_depth))) {
    fprintf(stderr, "%s\n", ot_error(rt));
    ot_destroy(rt);
    return 1;
  }
  struct sigaction action = {0}, previous;
  action.sa_handler = interrupt_handler;
  sigemptyset(&action.sa_mask);
  sigaddset(&action.sa_mask, SIGVTALRM);
  if (!signal_stack() || sigaction(SIGINT, &action, &previous)) {
    perror("sigaction");
    ot_destroy(rt);
    return 1;
  }
  ot_set_interrupt_flag(rt, &interrupt_requested);
  interrupt_runtime = rt;
  bool native = false;
#ifdef OTIUM_NATIVE
  /* Native code by default; --c.vm or OTIUM_VM=1 keeps the VM. */
  const char* vm = getenv("OTIUM_VM");
  native = !o.vm && !(vm && *vm && strcmp(vm, "0"));
  if (native && !ot_use_native(rt)) {
    fprintf(stderr, "cannot run native code: %s\n", ot_error(rt));
    ot_destroy(rt);
    return 1;
  }
#endif
  /* --c.unlock-core or OTIUM_UNLOCK_CORE=1 lets programs redefine core. */
  const char* unlock = getenv("OTIUM_UNLOCK_CORE");
  if (o.unlock_core || (unlock && *unlock && strcmp(unlock, "0"))) ot_unlock_core(rt);
  bool dev = o.dev_path != NULL;
  bool ok = ot_set_arguments(rt, o.script, (size_t)(argc - o.arguments),
                             (const char* const*)(argv + o.arguments));
  if (ok) ok = ot_load_expander(rt, OTIUM_LIBRARY_DIR);
  if (!ok) fprintf(stderr, "cannot load expander: %s\n", ot_error(rt));
  if (ok && !(ok = add_paths(rt, &o, argv)) && ot_error(rt)[0])
    fprintf(stderr, "%s\n", ot_error(rt));
  if (ok) {
    ok = ot_load_core(rt, OTIUM_LIBRARY_DIR);
    if (!ok) fprintf(stderr, "cannot load core: %s\n", ot_error(rt));
  }
  if (ok && (dev || o.server) && !(ok = load_dev(rt))) report_error(rt, "<dev>");
  if (ok && dev && !(ok = start_dev(rt, &o, native))) {
    int exit_status;
    if (!ot_interrupted(rt) && !ot_exit_status(rt, &exit_status)) report_error(rt, "<dev>");
    dev = false;
  }
  if (ok) ok = preload(rt, &o, argv);
  if (ok && o.script) ok = load(rt, o.script);
  if (ok && o.source) ok = evaluate(rt, o.source, strlen(o.source), "<expression>", true);
  if (ok && o.serve) {
    ot_value result;
    ok = ot_call(rt, "otium.dev/run", NULL, 0, &result);
    int exit_status;
    if (!ok && !ot_interrupted(rt) && !ot_exit_status(rt, &exit_status)) report_error(rt, "<dev>");
  }
  if (ok && o.server) ok = stdio_server(rt);
  if (ok && o.repl) ok = repl(rt, dev, native);
  if (dev) ot_set_interrupt_handler(rt, NULL);
  ot_poll_interrupt(rt);
  status = ot_interrupted(rt) ? 130 : ok ? 0 : 1;
  bool exiting = ot_exit_status(rt, &status);
  /* Stopping the server calls into Otium, which clears the exit request. */
  if (dev) stop_dev(rt);
  print_gc_stats(rt, &o);
  if ((ok || exiting) && !ot_flush_ports(rt)) {
    status = ot_interrupted(rt) ? 130 : 1;
    if (!ot_interrupted(rt) && !ot_exit_status(rt, &status))
      fprintf(stderr, "cannot flush output ports: %s\n", ot_error(rt));
  }
  sigaction(SIGINT, &previous, NULL);
  interrupt_runtime = NULL;
  ot_destroy(rt);
  return status;
}
