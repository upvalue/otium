#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include "reader.h"
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#ifndef OTIUM_LIBRARY_DIR
#define OTIUM_LIBRARY_DIR "lib"
#endif
static volatile sig_atomic_t interrupt_requested;

static void interrupt_handler(int signal_number) {
  (void)signal_number;
  interrupt_requested = 1;
}

#define SOURCE_LIMIT (16u * 1024u * 1024u)

static bool size_option(const char* text, size_t* out) {
  char* end;
  errno = 0;
  unsigned long long value = strtoull(text, &end, 10);
  if (errno || !isdigit((unsigned char)*text) || *end || value > SIZE_MAX || !value) return false;
  *out = (size_t)value;
  return true;
}

static bool evaluate(Runtime* rt, const char* source, size_t length, const char* path, bool echo) {
  Value result;
  if (!runtime_eval(rt, source, length, &result)) {
    int status;
    if (!runtime_interrupted(rt) && !runtime_exit_status(rt, &status))
      fprintf(stderr, "%s:%zu: %s\n", path, runtime_error_offset(rt), runtime_error(rt));
    return false;
  }
  if (echo) {
    runtime_print(rt, stdout, result);
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

static bool load(Runtime* rt, const char* path) {
  size_t length;
  char* buffer = read_source(path, &length);
  if (!buffer) return false;
  bool ok = evaluate(rt, buffer, length, path, false);
  free(buffer);
  return ok;
}

static bool project_paths(Runtime* rt, const char* path) {
  size_t length;
  char* source = read_source(path, &length);
  if (!source) return false;
  Reader reader;
  reader_init(&reader, source, length, SOURCE_LIMIT);
  bool ok = true;
  Ast* form;
  while (ok && (form = reader_next(&reader))) {
    if (form->kind != AST_LIST || !ast_symbol(form->as.list.first, "paths")) continue;
    if (form->as.list.tail) {
      ok = rt_error(rt, "project paths must be a proper list");
      break;
    }
    for (Ast* entry = form->as.list.first->next; ok && entry; entry = entry->next) {
      if (entry->kind != AST_STRING || memchr(entry->as.text.data, 0, entry->as.text.size)) {
        ok = rt_error(rt, "project paths must be strings without NUL");
        break;
      }
      const char* slash = strrchr(path, '/');
      size_t prefix = entry->as.text.size && entry->as.text.data[0] == '/' ? 0
                      : slash ? (size_t)(slash - path + 1)
                              : 0;
      char* directory = malloc(prefix + entry->as.text.size + 1);
      if (!directory) {
        ok = rt_error(rt, "out of host memory");
        break;
      }
      memcpy(directory, path, prefix);
      memcpy(directory + prefix, entry->as.text.data, entry->as.text.size);
      directory[prefix + entry->as.text.size] = 0;
      ok = runtime_load_path(rt, directory);
      free(directory);
    }
  }
  if (reader.error) ok = rt_coded_error(rt, reader.error_code, reader.error);
  reader_destroy(&reader);
  free(source);
  return ok;
}

static bool discover_project(Runtime* rt) {
  char* directory = getcwd(NULL, 0);
  if (!directory) return rt_error(rt, "cannot find current directory");
  char* path = malloc(strlen(directory) + sizeof("/project.ot"));
  bool ok = path != NULL;
  if (!ok) rt_error(rt, "out of host memory");
  while (ok) {
    sprintf(path, "%s/project.ot", directory);
    if (access(path, F_OK) == 0) {
      ok = project_paths(rt, path);
      break;
    }
    if (errno != ENOENT && errno != ENOTDIR) {
      ok = rt_error(rt, "cannot inspect project file");
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

static bool environment_paths(Runtime* rt) {
  const char* value = getenv("OTIUM_PATH");
  if (!value) return true;
  char* paths = strdup(value);
  if (!paths) return rt_error(rt, "out of host memory");
  bool ok = true;
  char* cursor = paths;
  while (ok && cursor) {
    char* next = strchr(cursor, ':');
    if (next) *next++ = 0;
    if (*cursor) ok = runtime_load_path(rt, cursor);
    cursor = next;
  }
  free(paths);
  return ok;
}

/* The REPL and program reads must share the standard port's managed buffer. */
static ssize_t input_line(Runtime* rt, char** line, size_t* capacity) {
  Value port, value;
  if (!runtime_call(rt, "otium.core/%standard-input", NULL, 0, &port) ||
      !runtime_call(rt, "otium.core/read-line", &port, 1, &value))
    return -2;
  if (value == V_EOF) return -1;
  if (!rt_kind(value, K_STRING)) {
    rt_error(rt, "read-line must return a string or EOF");
    return -2;
  }
  size_t length = rt_bytes_length(value);
  if (length >= SOURCE_LIMIT) {
    rt_error(rt, "source exceeds size limit");
    return -2;
  }
  if (*capacity < length + 1) {
    char* grown = realloc(*line, length + 1);
    if (!grown) {
      rt_error(rt, "out of host memory");
      return -2;
    }
    *line = grown;
    *capacity = length + 1;
  }
  memcpy(*line, rt_bytes_data(value), length);
  (*line)[length] = '\n';
  return (ssize_t)(length + 1);
}

static bool repl(Runtime* rt) {
  bool terminal = isatty(STDIN_FILENO), ok = true;
  char *line = NULL, *pending = NULL;
  size_t line_capacity = 0, length = 0;
  for (;;) {
    if (terminal) {
      fputs(length ? "... " : "> ", stdout);
      fflush(stdout);
    }
    ssize_t bytes = input_line(rt, &line, &line_capacity);
    if (bytes < 0) {
      if (bytes == -2) {
        if (!runtime_interrupted(rt)) fprintf(stderr, "stdin: %s\n", runtime_error(rt));
        ok = false;
      }
      if (length) {
        Reader reader;
        reader_init(&reader, pending, length, SOURCE_LIMIT);
        reader_next(&reader);
        fprintf(stderr, "<stdin>:%zu: %s\n", reader.error_offset,
                reader.error ? reader.error : "incomplete input");
        reader_destroy(&reader);
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
    Reader reader;
    reader_init(&reader, pending, length, SOURCE_LIMIT);
    size_t consumed = 0;
    for (;;) {
      size_t start = reader.position;
      Ast* form = reader_next(&reader);
      if (form) {
        evaluate(rt, pending + start, reader.position - start, "<stdin>", true);
        int status;
        if (runtime_interrupted(rt) || runtime_exit_status(rt, &status)) {
          reader_destroy(&reader);
          free(line);
          free(pending);
          return false;
        }
        consumed = reader.position;
        continue;
      }
      if (!reader.error) consumed = length;
      else if (!reader.incomplete) {
        fprintf(stderr, "<stdin>:%zu: %s\n", reader.error_offset, reader.error);
        consumed = length;
      }
      break;
    }
    reader_destroy(&reader);
    memmove(pending, pending + consumed, length - consumed);
    length -= consumed;
    fflush(stdout);
  }
  free(line);
  free(pending);
  return ok;
}

int main(int argc, char** argv) {
  size_t heap = 1024 * 1024, heap_max = 64 * 1024 * 1024, stack = 128 * 1024;
  bool heap_explicit = false, max_explicit = false;
  bool stats = false, interactive = false, preloaded = false, no_project = false;
  const char* project = NULL;
  const char *source = NULL, *path = NULL;
  int option_end = argc, arguments_start = argc;
  for (int i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "--")) {
      option_end = i;
      if (!source && !path) {
        if (i + 1 == argc) goto usage;
        path = argv[++i];
      }
      arguments_start = i + 1;
      break;
    } else if (!strcmp(argv[i], "--help") || !strcmp(argv[i], "-h")) {
      puts("Usage: otium-proto [OPTIONS] [SCRIPT] [-- ARGUMENTS...]\n"
           "  --path DIR, -L DIR     Add a module search directory\n"
           "  --project FILE         Read (paths ...) relative to FILE\n"
           "  --no-project           Skip upward project.ot discovery\n"
           "  --load FILE            Load before the script; repeatable\n"
           "  --repl                 Enter the REPL after loading\n"
           "  --c.heap-init=BYTES     Initial semispace size (minimum 1024)\n"
           "  --c.heap-max=BYTES      Maximum semispace size\n"
           "  --heap=BYTES            Fixed semispace unless --heap-max is given\n"
           "  --heap-max=BYTES        Maximum semispace size\n"
           "  --stack=SLOTS           VM stack limit\n"
           "  --stats                Print VM statistics\n"
           "  -e SOURCE              Evaluate and print SOURCE\n"
           "  --help, -h             Show this help");
      return 0;
    } else if (!strncmp(argv[i], "--c.heap-init=", 14)) {
      if (!size_option(argv[i] + 14, &heap) || heap < 1024) goto usage;
    } else if (!strncmp(argv[i], "--c.heap-max=", 13)) {
      if (!size_option(argv[i] + 13, &heap_max)) goto usage;
      max_explicit = true;
    } else if (!strcmp(argv[i], "--no-project")) no_project = true;
    else if (!strcmp(argv[i], "--project") && i + 1 < argc) project = argv[++i];
    else if (!strncmp(argv[i], "--project=", 10)) project = argv[i] + 10;
    else if (!strncmp(argv[i], "--path=", 7)) continue;
    else if (!strncmp(argv[i], "--load=", 7)) preloaded = true;
    else if (!strncmp(argv[i], "--heap=", 7)) {
      if (!size_option(argv[i] + 7, &heap)) goto usage;
      heap_explicit = true;
    } else if (!strncmp(argv[i], "--heap-max=", 11)) {
      if (!size_option(argv[i] + 11, &heap_max)) goto usage;
      max_explicit = true;
    } else if (!strncmp(argv[i], "--stack=", 8)) {
      if (!size_option(argv[i] + 8, &stack)) goto usage;
    } else if ((!strcmp(argv[i], "-L") || !strcmp(argv[i], "--path") ||
                !strcmp(argv[i], "--load")) &&
               i + 1 < argc) {
      if (!strcmp(argv[i], "--load")) preloaded = true;
      i++;
    } else if (!strcmp(argv[i], "--stats")) stats = true;
    else if (!strcmp(argv[i], "--repl")) interactive = true;
    else if (!strcmp(argv[i], "-e") && i + 1 < argc && !source && !path) source = argv[++i];
    else if (argv[i][0] != '-' && !source && !path) path = argv[i];
    else goto usage;
  }
  if (project && no_project) goto usage;
  if (!source && !path && !preloaded) interactive = true;
  if (heap_explicit && !max_explicit) heap_max = heap;
  if (heap_max < heap || heap_max > SIZE_MAX / 2) goto usage;
  Runtime* rt = runtime_create(heap, stack);
  if (!rt) {
    fputs("cannot initialize runtime\n", stderr);
    return 1;
  }
  if (!runtime_heap_limit(rt, heap_max)) {
    fprintf(stderr, "%s\n", runtime_error(rt));
    runtime_destroy(rt);
    return 1;
  }
  struct sigaction action = {0}, previous;
  action.sa_handler = interrupt_handler;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGINT, &action, &previous)) {
    perror("sigaction");
    runtime_destroy(rt);
    return 1;
  }
  runtime_interrupt_flag(rt, &interrupt_requested);
  bool ok = runtime_set_arguments(rt, path, (size_t)(argc - arguments_start),
                                  (const char* const*)(argv + arguments_start));
  if (ok) ok = runtime_load_expander(rt, OTIUM_LIBRARY_DIR);
  if (!ok) fprintf(stderr, "cannot load expander: %s\n", runtime_error(rt));
  for (int i = 1; ok && i < option_end; i++) {
    if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "--load") || !strcmp(argv[i], "--project")) {
      i++;
      continue;
    }
    if (!strcmp(argv[i], "-L") || !strcmp(argv[i], "--path")) {
      ok = runtime_load_path(rt, argv[++i]);
      if (!ok) fprintf(stderr, "%s\n", runtime_error(rt));
    }
    if (!strncmp(argv[i], "--path=", 7)) ok = runtime_load_path(rt, argv[i] + 7);
  }
  if (ok) ok = environment_paths(rt);
  if (ok && !no_project) ok = project ? project_paths(rt, project) : discover_project(rt);
  if (!ok && runtime_error(rt)[0]) fprintf(stderr, "%s\n", runtime_error(rt));
  if (ok) {
    ok = runtime_load_core(rt, OTIUM_LIBRARY_DIR);
    if (!ok) fprintf(stderr, "cannot load core: %s\n", runtime_error(rt));
  }
  for (int i = 1; ok && i < option_end; i++) {
    if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "-L") || !strcmp(argv[i], "--path") ||
        !strcmp(argv[i], "--project")) {
      i++;
      continue;
    }
    if (!strcmp(argv[i], "--load")) ok = load(rt, argv[++i]);
    else if (!strncmp(argv[i], "--load=", 7)) ok = load(rt, argv[i] + 7);
  }
  if (ok && path) ok = load(rt, path);
  if (ok && source) ok = evaluate(rt, source, strlen(source), "<expression>", true);
  if (ok && interactive) ok = repl(rt);
  if (stats)
    fprintf(stderr, "collections=%zu max-frames=%zu heap-capacity=%zu\n", runtime_collections(rt),
            runtime_max_frames(rt), runtime_heap_capacity(rt));
  if (stats && runtime_foreign_signatures(rt))
    fprintf(stderr, "ffi-signatures=%zu\n", runtime_foreign_signatures(rt));
  runtime_poll_interrupt(rt);
  int status = runtime_interrupted(rt) ? 130 : ok ? 0 : 1;
  bool exiting = runtime_exit_status(rt, &status);
  if ((ok || exiting) && !runtime_flush_ports(rt)) {
    status = runtime_interrupted(rt) ? 130 : 1;
    if (!runtime_interrupted(rt) && !runtime_exit_status(rt, &status))
      fprintf(stderr, "cannot flush output ports: %s\n", runtime_error(rt));
  }
  sigaction(SIGINT, &previous, NULL);
  runtime_destroy(rt);
  return status;
usage:
  fputs("usage: otium-proto [--heap=BYTES] [--heap-max=BYTES] [--stack=SLOTS] [--stats] [-L DIR] "
        "[--load FILE] [--repl] [FILE | -e SOURCE] [-- ARG ...]\n",
        stderr);
  return 2;
}
