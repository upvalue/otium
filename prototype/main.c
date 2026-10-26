#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include "reader.h"
#include <errno.h>
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
  if (errno || !*text || *text == '-' || *end || value > SIZE_MAX || !value) return false;
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

static bool load(Runtime* rt, const char* path) {
  FILE* file = fopen(path, "rb");
  if (!file) {
    perror(path);
    return false;
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
  if (ok) ok = evaluate(rt, buffer, length, path, false);
  free(buffer);
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
  bool stats = false, interactive = false;
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
    } else if (!strncmp(argv[i], "--heap=", 7)) {
      if (!size_option(argv[i] + 7, &heap)) goto usage;
      heap_explicit = true;
    } else if (!strncmp(argv[i], "--heap-max=", 11)) {
      if (!size_option(argv[i] + 11, &heap_max)) goto usage;
      max_explicit = true;
    } else if (!strncmp(argv[i], "--stack=", 8)) {
      if (!size_option(argv[i] + 8, &stack)) goto usage;
    } else if ((!strcmp(argv[i], "-L") || !strcmp(argv[i], "--path") ||
                !strcmp(argv[i], "--load")) &&
               i + 1 < argc)
      i++;
    else if (!strcmp(argv[i], "--stats")) stats = true;
    else if (!strcmp(argv[i], "--repl")) interactive = true;
    else if (!strcmp(argv[i], "-e") && i + 1 < argc && !source && !path) source = argv[++i];
    else if (argv[i][0] != '-' && !source && !path) path = argv[i];
    else goto usage;
  }
  if (!source && !path) interactive = true;
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
    if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "--load")) {
      i++;
      continue;
    }
    if (!strcmp(argv[i], "-L") || !strcmp(argv[i], "--path")) {
      ok = runtime_load_path(rt, argv[++i]);
      if (!ok) fprintf(stderr, "%s\n", runtime_error(rt));
    }
  }
  if (ok) {
    ok = runtime_load_core(rt, OTIUM_LIBRARY_DIR);
    if (!ok) fprintf(stderr, "cannot load core: %s\n", runtime_error(rt));
  }
  for (int i = 1; ok && i < option_end; i++) {
    if (!strcmp(argv[i], "-e") || !strcmp(argv[i], "-L") || !strcmp(argv[i], "--path")) {
      i++;
      continue;
    }
    if (!strcmp(argv[i], "--load")) ok = load(rt, argv[++i]);
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
