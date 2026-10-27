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

static bool selected(const char* suite, const char* name, int argc, char** argv, int first) {
  if (first >= argc) return true;
  char full[256];
  snprintf(full, sizeof(full), "%s/%s", suite, name);
  for (int i = first; i < argc; i++) {
    if (strchr(argv[i], '/') ? !strncmp(full, argv[i], strlen(argv[i])) : !!strstr(full, argv[i]))
      return true;
  }
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

int test_main(int argc, char** argv, const test_suite* const* suites, size_t count) {
  bool fork_tests = true;
  int first = 1;
  if (argc > 1 && !strcmp(argv[1], "--no-fork")) {
    fork_tests = false;
    first = 2;
  }
  size_t passed = 0, failed = 0;
  for (size_t s = 0; s < count; s++) {
    size_t suite_passed = 0, suite_failed = 0;
    for (size_t i = 0; i < suites[s]->count; i++) {
      const test_case* test = &suites[s]->tests[i];
      if (!selected(suites[s]->name, test->name, argc, argv, first)) continue;
      current_test = test->name;
      bool ok = true;
      if (fork_tests) ok = run_forked(test);
      else test->run();
      if (!ok) printf("FAIL %s/%s\n", suites[s]->name, test->name);
      fflush(stdout);
      if (ok) suite_passed++;
      else suite_failed++;
    }
    if (suite_passed || suite_failed)
      printf("%s: %zu passed, %zu failed\n", suites[s]->name, suite_passed, suite_failed);
    passed += suite_passed;
    failed += suite_failed;
  }
  printf("%zu passed, %zu failed\n", passed, failed);
  return failed || !passed ? 1 : 0;
}
