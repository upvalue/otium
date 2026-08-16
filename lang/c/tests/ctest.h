// ctest.h — minimal test harness for the C port: auto-registered TEST()
// functions, CHECK macros, and a fork-based death-test helper (substrate
// tests assert that ot_fatal aborts the process).
#pragma once
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <sys/wait.h>
#include <unistd.h>

typedef void (*CtestFn)(void);
typedef struct CtestCase {
  const char* name;
  CtestFn fn;
} CtestCase;

#define CTEST_MAX_CASES 512
extern CtestCase ctest_cases[CTEST_MAX_CASES];
extern int ctest_case_count;
extern int ctest_failures;
extern const char* ctest_current;

#define TEST(name)                                                                                 \
  static void ctest_##name(void);                                                                  \
  __attribute__((constructor)) static void ctest_reg_##name(void) {                                \
    if (ctest_case_count < CTEST_MAX_CASES)                                                        \
      ctest_cases[ctest_case_count++] = (CtestCase){#name, ctest_##name};                          \
  }                                                                                                \
  static void ctest_##name(void)

#define CTEST_FAIL(fmt, ...)                                                                       \
  do {                                                                                             \
    ctest_failures++;                                                                              \
    fprintf(stderr, "FAIL %s (%s:%d): " fmt "\n", ctest_current, __FILE__, __LINE__,               \
            ##__VA_ARGS__);                                                                        \
  } while (0)

#define CHECK(cond)                                                                                \
  do {                                                                                             \
    if (!(cond)) CTEST_FAIL("CHECK(%s)", #cond);                                                   \
  } while (0)

#define CHECK_EQ(a, b)                                                                             \
  do {                                                                                             \
    long long _a = (long long)(a), _b = (long long)(b);                                            \
    if (_a != _b) CTEST_FAIL("CHECK_EQ(%s, %s): %lld != %lld", #a, #b, _a, _b);                    \
  } while (0)

#define CHECK_STR(a, b)                                                                            \
  do {                                                                                             \
    const char* _a = (a);                                                                          \
    const char* _b = (b);                                                                          \
    if (strcmp(_a, _b) != 0) CTEST_FAIL("CHECK_STR(%s, %s): \"%s\" != \"%s\"", #a, #b, _a, _b);    \
  } while (0)

// Compare a non-NUL-terminated (ptr,len) region against a C string literal.
#define CHECK_MEM(ptr, len, lit)                                                                   \
  do {                                                                                             \
    const char* _p = (ptr);                                                                        \
    unsigned _n = (unsigned)(len);                                                                 \
    if (_n != (unsigned)strlen(lit) || memcmp(_p, (lit), _n) != 0)                                 \
      CTEST_FAIL("CHECK_MEM(%s): \"%.*s\" != \"%s\"", #ptr, (int)_n, _p, (lit));                   \
  } while (0)

// Runs `stmt` in a forked child and checks the child aborted (ot_fatal).
#define CHECK_ABORTS(stmt)                                                                         \
  do {                                                                                             \
    pid_t _pid = fork();                                                                           \
    if (_pid == 0) {                                                                               \
      fclose(stderr); /* silence the fatal message */                                              \
      stmt;                                                                                        \
      _exit(0); /* reaching here means no abort */                                                 \
    }                                                                                              \
    int _status = 0;                                                                               \
    waitpid(_pid, &_status, 0);                                                                    \
    if (!(WIFSIGNALED(_status) && WTERMSIG(_status) == SIGABRT))                                   \
      CTEST_FAIL("CHECK_ABORTS(%s): child did not abort", #stmt);                                  \
  } while (0)

#ifdef CTEST_MAIN
CtestCase ctest_cases[CTEST_MAX_CASES];
int ctest_case_count = 0;
int ctest_failures = 0;
const char* ctest_current = "";

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : NULL;
  int ran = 0;
  for (int i = 0; i < ctest_case_count; i++) {
    if (filter && !strstr(ctest_cases[i].name, filter)) continue;
    ctest_current = ctest_cases[i].name;
    int before = ctest_failures;
    ctest_cases[i].fn();
    ran++;
    if (ctest_failures == before) fprintf(stderr, "ok   %s\n", ctest_current);
  }
  fprintf(stderr, "%d test(s) ran, %d failure(s)\n", ran, ctest_failures);
  return ctest_failures ? 1 : 0;
}
#endif
