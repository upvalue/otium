#define _POSIX_C_SOURCE 200809L
#include "test.h"
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static const char* current_test;

bool test_same_string(const char* a, const char* b) { return a && b && !strcmp(a, b); }

void test_fail(const char* file, int line, const char* format, ...) {
  fprintf(stderr, "%s:%d: %s: ", file, line, current_test ? current_test : "test");
  va_list arguments;
  va_start(arguments, format);
  vfprintf(stderr, format, arguments);
  va_end(arguments);
  fputc('\n', stderr);
  fflush(NULL);
  exit(1);
}

static bool selected(const char* name, int argc, char** argv, int first) {
  if (first >= argc) return true;
  for (int i = first; i < argc; i++)
    if (strstr(name, argv[i])) return true;
  return false;
}

/* Run one test in a child process and describe how it ended. */
static bool run_forked(const test_case* test) {
  fflush(NULL);
  pid_t child = fork();
  if (child < 0) {
    perror("fork");
    return false;
  }
  if (child == 0) {
    test->run();
    fflush(NULL);
    _exit(0);
  }
  int status;
  if (waitpid(child, &status, 0) < 0) {
    perror("waitpid");
    return false;
  }
  if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return true;
  if (WIFSIGNALED(status))
    fprintf(stderr, "%s: killed by signal %d\n", test->name, WTERMSIG(status));
  return false;
}

int test_main(int argc, char** argv, const test_case* tests, size_t count) {
  bool fork_tests = true;
  int first = 1;
  if (argc > 1 && !strcmp(argv[1], "--no-fork")) {
    fork_tests = false;
    first = 2;
  }
  size_t passed = 0, failed = 0;
  for (size_t i = 0; i < count; i++) {
    if (!selected(tests[i].name, argc, argv, first)) continue;
    current_test = tests[i].name;
    bool ok = true;
    if (fork_tests) ok = run_forked(&tests[i]);
    else tests[i].run();
    printf("%s %s\n", ok ? "ok" : "FAIL", tests[i].name);
    fflush(stdout);
    if (ok) passed++;
    else failed++;
  }
  printf("%s: %zu passed, %zu failed\n", argv[0], passed, failed);
  return failed || !passed ? 1 : 0;
}
