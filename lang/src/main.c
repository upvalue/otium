#define OT_INTERNAL
#include "otium.h"

#include "bestline.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static bool path_read(const char* directory, const char* relative, char** source, size_t* length) {
  bytes path = {0};
  bytes_append(&path, directory, strlen(directory));
  if (path.length != 0 && path.data[path.length - 1] != '/') bytes_append(&path, "/", 1);
  bytes_append(&path, relative, strlen(relative));
  bytes_append(&path, "\0", 1);
  bool found = ot_platform_read_file(path.data, source, length);
  ot_host_free(path.data);
  return found;
}

static bool module_loader(void* userdata, const char* namespace_name, char** source,
                          size_t* length) {
  (void)userdata;
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
      if (path_read(load_path.values[i], relative.data, source, length)) {
        ot_host_free(relative.data);
        return true;
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
  otv result;
  bool ok = ot_eval_src(state, source, length, path, &result);
  ot_host_free(source);
  if (ok) return 0;
  if (state->current_process->unwind_kind == UNWIND_QUIT) {
    ot_clear_condition(state);
    return state->quit_requested ? 0 : 130;
  }
  fputs("otium: ", stderr);
  print_value(state, ot_condition(state), stderr);
  return 1;
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
      } else if (state->current_process->unwind_kind == UNWIND_RESTART ||
                 state->current_process->unwind_kind == UNWIND_QUIT) {
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
  bytes request = {0};
  ot_set_interrupt_hook(state, run_server_break, NULL);
  while (read_server_request(&request)) {
    bool stop = false;
    if (request.length != 0) {
      otv result;
      if (ot_eval_src(state, request.data, request.length, "<server>", &result)) {
        print_value(state, result, stdout);
      } else if (state->current_process->unwind_kind == UNWIND_QUIT) {
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
  if (state->current_process->unwind_kind == UNWIND_QUIT) {
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

static void run_repl(ots* state) {
  fputs("otium repl - ^C interrupts, ^D exits\n", stdout);
  bytes pending = {0};
  bool interactive = isatty(STDIN_FILENO);
  for (;;) {
    char fixed[4096];
    char* line;
    if (interactive) {
      line = bestlineWithHistory(pending.length == 0 ? "ot> " : "..> ", "otium");
      if (line == NULL) break;
    } else {
      if (fgets(fixed, sizeof fixed, stdin) == NULL) break;
      line = fixed;
    }
    if (pending.length == 0 && blank_line(line)) {
      if (interactive) bestlineFree(line);
      continue;
    }
    if (pending.length != 0 && pending.data[pending.length - 1] != '\n')
      bytes_append(&pending, "\n", 1);
    bytes_append(&pending, line, strlen(line));
    if (interactive) bestlineFree(line);
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
  if (interactive) {
    bestlineHistoryFree();
    fputc('\n', stdout);
  }
}

static bool parse_number(const char* option, const char* text, size_t* out) {
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

static void usage(FILE* stream) {
  fputs("Usage: otium [OPTIONS] [FILE ...]\n"
        "  --repl             start a REPL after FILEs\n"
        "  --server           run the framed stdio server\n"
        "  --path DIR         add a module search directory\n"
        "  --project FILE     read load paths from FILE\n"
        "  --no-project       skip project.ot discovery\n"
        "  --heap-init BYTES  initial semispace size\n"
        "  --heap-max BYTES   maximum semispace size\n"
        "  --max-depth N      VM frame depth limit\n"
        "  --gc-stats         print collector statistics at exit\n"
        "  -h, --help         show this help\n",
        stream);
}

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
  fprintf(stderr, "GC stats:\n  allocations: %" PRIu64 "\n", stats.allocations);
  print_byte_stat("allocated bytes", stats.allocated_bytes);
  fprintf(stderr, "  collections: %" PRIu64 "\n", stats.collections);
  print_byte_stat("copied bytes", stats.copied_bytes);
  print_byte_stat("reclaimed bytes", stats.reclaimed_bytes);
  print_byte_stat("heap used bytes", stats.used_bytes);
  print_byte_stat("peak heap used bytes", stats.peak_used_bytes);
  print_byte_stat("heap capacity bytes", stats.capacity_bytes);
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
  bool stats = false;
  bool no_project = false;
  const char* project = NULL;
  bool positional = false;
  for (int i = 1; i < argc; i++) {
    const char* argument = argv[i];
    if (!positional && strcmp(argument, "--") == 0) positional = true;
    else if (!positional && strcmp(argument, "--repl") == 0) repl = true;
    else if (!positional && strcmp(argument, "--server") == 0) server = true;
    else if (!positional && strcmp(argument, "--gc-stats") == 0) stats = true;
    else if (!positional && strcmp(argument, "--no-project") == 0) no_project = true;
    else if (!positional && strcmp(argument, "--help") == 0) {
      usage(stdout);
      return 0;
    } else if (!positional && strcmp(argument, "-h") == 0) {
      usage(stdout);
      return 0;
    } else if (!positional &&
               (strcmp(argument, "--path") == 0 || strcmp(argument, "--project") == 0 ||
                strcmp(argument, "--heap-init") == 0 || strcmp(argument, "--heap-max") == 0 ||
                strcmp(argument, "--max-depth") == 0 || strcmp(argument, "--stack-slots") == 0)) {
      if (++i == argc) {
        fprintf(stderr, "otium: %s requires %s\n", argument,
                strcmp(argument, "--path") == 0      ? "a directory"
                : strcmp(argument, "--project") == 0 ? "a file"
                                                     : "a value");
        return 2;
      }
      if (strcmp(argument, "--path") == 0) strings_push(&load_path, argv[i], strlen(argv[i]));
      else if (strcmp(argument, "--project") == 0) project = argv[i];
      else {
        size_t value;
        if (!parse_number(argument, argv[i], &value)) return 2;
        if (strcmp(argument, "--heap-init") == 0) config.heap_init = value;
        else if (strcmp(argument, "--heap-max") == 0) config.heap_max = value;
        else if (strcmp(argument, "--max-depth") == 0) config.max_depth = (unsigned)value;
      }
    } else if (!positional && argument[0] == '-' && argument[1] != '\0') {
      fprintf(stderr, "otium: unknown option %s\n", argument);
      return 2;
    } else {
      strings_push(&files, argument, strlen(argument));
    }
  }
  if (repl && server) {
    fputs("otium: --repl and --server cannot be used together\n", stderr);
    return 2;
  }
  if (no_project && project != NULL) {
    fputs("otium: --project and --no-project cannot be used together\n", stderr);
    return 2;
  }
  if (config.heap_init < 1024) {
    fputs("otium: --heap-init must be at least 1024 bytes\n", stderr);
    return 2;
  }
  if (config.heap_max < config.heap_init) {
    fputs("otium: --heap-max must be at least --heap-init\n", stderr);
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

  ots* state = ot_create(&config);
  if (state == NULL) {
    fputs("otium: runtime creation failed\n", stderr);
    return 1;
  }
  signal_state = state;
  if (getenv("OTIUM_GC_STRESS") != NULL) state->config.gc_stress = true;
  ot_set_writer(state, stdout_writer, NULL);
  ot_set_loader(state, module_loader, NULL);
  ot_register_demo_extension(state);
#ifdef OT_WITH_RAY
  ot_register_ray_extension(state);
#endif
  struct sigaction action = {0};
  action.sa_handler = handle_interrupt;
  sigaction(SIGINT, &action, NULL);

  int status = 0;
  for (size_t i = 0; i < files.length && status == 0 && !state->quit_requested; i++)
    status = run_file(state, files.values[i]);
  if (status == 0 && !state->quit_requested) {
    if (server) run_server(state);
    else if (repl || files.length == 0) run_repl(state);
  }
  if (stats) print_gc_stats(state);
  ot_destroy(state);
  signal_state = NULL;
  for (size_t i = 0; i < files.length; i++) ot_host_free(files.values[i]);
  ot_host_free(files.values);
  for (size_t i = 0; i < load_path.length; i++) ot_host_free(load_path.values[i]);
  ot_host_free(load_path.values);
  return status;
}
