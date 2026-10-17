#define _POSIX_C_SOURCE 200809L
#include "line-edit.h"
#include "bestline.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct binding {
  char* name;
  char* documentation;
} binding;

static binding* bindings;
static size_t binding_count;
static size_t binding_capacity;

void ot_line_bindings_clear(void) {
  for (size_t i = 0; i < binding_count; ++i) {
    free(bindings[i].name);
    free(bindings[i].documentation);
  }
  binding_count = 0;
}

void ot_line_binding(const char* name, const char* documentation) {
  for (size_t i = 0; i < binding_count; ++i)
    if (strcmp(bindings[i].name, name) == 0) return;
  if (binding_count == binding_capacity) {
    size_t capacity = binding_capacity ? binding_capacity * 2 : 128;
    binding* grown = realloc(bindings, capacity * sizeof(*grown));
    if (!grown) return;
    bindings = grown;
    binding_capacity = capacity;
  }
  char* copied_name = strdup(name);
  char* copied_doc = strdup(documentation ? documentation : "");
  if (!copied_name || !copied_doc) {
    free(copied_name);
    free(copied_doc);
    return;
  }
  bindings[binding_count++] = (binding){copied_name, copied_doc};
}

static int compare_bindings(const void* a, const void* b) {
  return strcmp(((const binding*)a)->name, ((const binding*)b)->name);
}

static int token_boundary(unsigned char c) {
  return isspace(c) || strchr("()[]{}\"'`,;", c) != NULL;
}

static void complete(const char* input, int position, bestlineCompletions* results) {
  if (position < 0 || (size_t)position > strlen(input)) return;
  /* Do not turn string contents or line comments into binding names. */
  int quoted = 0, escaped = 0, comment = 0;
  for (int i = 0; i < position; ++i) {
    char c = input[i];
    if (comment) {
      if (c == '\n') comment = 0;
      continue;
    }
    if (escaped) {
      escaped = 0;
      continue;
    }
    if (quoted && c == '\\') {
      escaped = 1;
      continue;
    }
    if (c == '\"') quoted = !quoted;
    if (!quoted && c == ';') comment = 1;
  }
  if (quoted || comment) return;
  size_t start = (size_t)position;
  while (start && !token_boundary((unsigned char)input[start - 1])) --start;
  if (start == 1 && input[0] == ',') start = 0;
  size_t prefix = (size_t)position - start;
  if (!prefix) return;
  size_t end = (size_t)position;
  if (input[end] && !token_boundary((unsigned char)input[end])) return;
  for (size_t i = 0; i < binding_count; ++i) {
    const char* name = bindings[i].name;
    if (strncmp(name, input + start, prefix) != 0) continue;
    size_t length = start + strlen(name) + strlen(input + end) + 1;
    char* candidate = malloc(length);
    if (!candidate) return;
    memcpy(candidate, input, start);
    strcpy(candidate + start, name);
    strcat(candidate, input + end);
    bestlineAddCompletion(results, candidate);
    free(candidate);
  }
}

static int help_command(const char* input) {
  while (isspace((unsigned char)*input)) ++input;
  if (strncmp(input, ",help", 5) || (input[5] && !isspace((unsigned char)input[5]))) return 0;
  input += 5;
  while (isspace((unsigned char)*input)) ++input;
  size_t length = strlen(input);
  while (length && isspace((unsigned char)input[length - 1])) --length;
  if (!length) {
    puts("Tab completes visible names; repeated Tab cycles matches.\n"
         ",help NAME shows documentation; ,help PREFIX lists names.\n"
         "Ctrl-R searches history; Ctrl-X Ctrl-E opens $VISUAL/$EDITOR.\n"
         "Ctrl-C cancels input; Ctrl-D on an empty line exits.");
    return 1;
  }
  for (size_t i = 0; i < binding_count; ++i) {
    const binding* entry = bindings + i;
    if (strlen(entry->name) != length || strncmp(entry->name, input, length)) continue;
    const char* doc = entry->documentation;
    printf("%s: %s\n", entry->name, *doc ? doc : "No documentation recorded.");
    return 1;
  }
  size_t matches = 0;
  for (size_t i = 0; i < binding_count; ++i)
    if (!strncmp(bindings[i].name, input, length)) {
      puts(bindings[i].name);
      ++matches;
    }
  if (!matches) printf("No binding or help topic named %.*s.\n", (int)length, input);
  return 1;
}

static char* line;
static int interrupted;

static void reading_interrupt(int number) { (void)number; }

int ot_line_interrupted(void) { return interrupted; }

static char* edit_file(const char* input) {
  const char* editor = getenv("VISUAL");
  if (!editor || !*editor) editor = getenv("EDITOR");
  if (!editor || !*editor) editor = "vi";
  const char* tmp = getenv("TMPDIR");
  if (!tmp || !*tmp) tmp = "/tmp";
  char* path = malloc(strlen(tmp) + 32);
  char* command = malloc(strlen(editor) + 16);
  char* result = NULL;
  FILE* file = NULL;
  int status = 0;
  if (!path || !command) goto done;
  sprintf(path, "%s/otium-edit-XXXXXX", tmp);
  int fd = mkstemp(path);
  if (fd < 0) goto done;
  file = fdopen(fd, "w");
  if (!file) {
    close(fd);
    goto remove_file;
  }
  int written = fputs(input, file);
  int closed = fclose(file);
  file = NULL;
  if (written == EOF || closed != 0) goto remove_file;

  /* The editor is a shell command; the generated filename is a separate argument. */
  sprintf(command, "%s \"$1\"", editor);
  struct sigaction ignore = {0}, old_int, old_quit;
  ignore.sa_handler = SIG_IGN;
  sigemptyset(&ignore.sa_mask);
  sigaction(SIGINT, &ignore, &old_int);
  sigaction(SIGQUIT, &ignore, &old_quit);
  pid_t child = fork();
  if (child == 0) {
    signal(SIGINT, SIG_DFL);
    signal(SIGQUIT, SIG_DFL);
    execl("/bin/sh", "sh", "-c", command, "otium-editor", path, (char*)NULL);
    _exit(127);
  }
  pid_t waited = -1;
  if (child > 0) {
    do {
      waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
  }
  sigaction(SIGQUIT, &old_quit, NULL);
  sigaction(SIGINT, &old_int, NULL);
  if (waited < 0 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    fputs("otium: editor failed; keeping original input\n", stderr);
    goto remove_file;
  }
  file = fopen(path, "rb");
  if (!file || fseek(file, 0, SEEK_END) != 0) goto remove_file;
  long length = ftell(file);
  if (length < 0 || fseek(file, 0, SEEK_SET) != 0) goto remove_file;
  result = malloc((size_t)length + 1);
  if (!result) goto remove_file;
  if (fread(result, 1, (size_t)length, file) != (size_t)length ||
      memchr(result, '\0', (size_t)length)) {
    free(result);
    result = NULL;
    goto remove_file;
  }
  /* Editors normally add a final newline. Keep embedded newlines. */
  while (length > 0 && (result[length - 1] == '\n' || result[length - 1] == '\r')) --length;
  result[length] = '\0';
remove_file:
  if (file) fclose(file);
  unlink(path);
done:
  if (!result) fputs("otium: could not read edited input; keeping original input\n", stderr);
  free(command);
  free(path);
  return result;
}

int ot_line_interactive(void) { return isatty(STDIN_FILENO) && isatty(STDOUT_FILENO); }

const char* ot_line_read(const char* prompt) {
  free(line);
  line = NULL;
  bestlineSetEditorCallback(edit_file);
  bestlineSetCompletionCallback(complete);
  ot_line_binding(",help", ",help NAME shows binding documentation; ,help PREFIX lists matching names.");
  qsort(bindings, binding_count, sizeof(*bindings), compare_bindings);
  struct sigaction action = {0}, previous;
  action.sa_handler = reading_interrupt;
  sigemptyset(&action.sa_mask);
  sigaction(SIGINT, &action, &previous);
  errno = 0;
  const char* history = getenv("OTIUM_HISTORY");
  if (!history) history = "otium";
  do {
    free(line);
    line = ot_line_interactive() ? bestlineWithHistory(prompt, history) : bestline(NULL);
  } while (line && strcmp(prompt ? prompt : "", "..> ") && help_command(line));
  interrupted = !line && errno == EINTR;
  sigaction(SIGINT, &previous, NULL);
  if (interrupted) return "";
  return line;
}

void ot_line_close(void) {
  free(line);
  line = NULL;
  bestlineHistoryFree();
  ot_line_bindings_clear();
  free(bindings);
  bindings = NULL;
  binding_capacity = 0;
}
