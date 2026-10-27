#ifndef OTIUM_TEST_H
#define OTIUM_TEST_H

/* A small test runner. Each test file lists its tests in a table and ends with
 * TEST_SUITE(name, table); all suites link into one program. Every test runs
 * in a forked child, so a failed CHECK, an abort or a crash ends only that test.
 *
 * Usage: tests [--no-fork] [PATTERN...]
 *   PATTERN runs only tests whose "suite/test" name contains it. A pattern
 *   with a slash must match from the start, so "heap/" runs one suite.
 *   --no-fork runs tests in this process and stops at the first failure,
 *   which is what a debugger or a script expecting a signal wants. */

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct {
  const char* name;
  void (*run)(void);
} test_case;

#define TEST(name) static void name(void)
#define TEST_ENTRY(name) {#name, name}

typedef struct {
  const char* name;
  const test_case* tests;
  size_t count;
} test_suite;

/* Defines the suite's symbol, name_suite. */
#define TEST_SUITE(name, table)                                                                    \
  const test_suite name##_suite = {#name, table, sizeof(table) / sizeof(*(table))}

/* Run the selected tests. The build generates main, which lists every suite. */
int test_main(int argc, char** argv, const test_suite* const* suites, size_t count);

/* Report a failure and end the current test. */
_Noreturn void test_fail(const char* file, int line, const char* format, ...);

#define CHECK(condition)                                                                           \
  do {                                                                                             \
    if (!(condition)) test_fail(__FILE__, __LINE__, "%s", #condition);                             \
  } while (0)

#define CHECK_EQ_INT(actual, expected)                                                             \
  do {                                                                                             \
    long long check_actual_ = (long long)(actual);                                                 \
    long long check_expected_ = (long long)(expected);                                             \
    if (check_actual_ != check_expected_)                                                          \
      test_fail(__FILE__, __LINE__, "%s == %s: got %lld, expected %lld", #actual, #expected,       \
                check_actual_, check_expected_);                                                   \
  } while (0)

#define CHECK_STR(actual, expected)                                                                \
  do {                                                                                             \
    const char* check_actual_ = (actual);                                                          \
    const char* check_expected_ = (expected);                                                      \
    if (!test_same_string(check_actual_, check_expected_))                                         \
      test_fail(__FILE__, __LINE__, "%s: got \"%s\", expected \"%s\"", #actual,                    \
                check_actual_ ? check_actual_ : "(null)", check_expected_);                        \
  } while (0)

bool test_same_string(const char* a, const char* b);

#endif
