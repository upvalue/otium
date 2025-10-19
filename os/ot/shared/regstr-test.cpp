// regstr-test.cpp - unit tests for regstr
#include "ot/shared/regstr.hpp"
#include "vendor/doctest.h"

TEST_CASE("regstr: constructor from uint32_t values") {
  regstr r(0x6c6c6568, 0x0000006f); // "hell" and "o"
  char buf[8];
  size_t len = r.extract(buf);

  CHECK(len == 5);
  CHECK(buf[0] == 'h');
  CHECK(buf[1] == 'e');
  CHECK(buf[2] == 'l');
  CHECK(buf[3] == 'l');
  CHECK(buf[4] == 'o');
  CHECK(buf[5] == '\0');
}

TEST_CASE("regstr: constructor from string - hello") {
  regstr r("hello");
  char buf[8];
  size_t len = r.extract(buf);

  CHECK(len == 5);
  CHECK(strcmp(buf, "hello") == 0);
}

TEST_CASE("regstr: constructor from string - single char") {
  regstr r("a");
  char buf[8];
  size_t len = r.extract(buf);

  CHECK(len == 1);
  CHECK(buf[0] == 'a');
  CHECK(buf[1] == '\0');
}

TEST_CASE("regstr: constructor from string - max length (8 chars)") {
  regstr r("12345678");
  char buf[8];
  size_t len = r.extract(buf);

  CHECK(len == 8);
  CHECK(buf[0] == '1');
  CHECK(buf[1] == '2');
  CHECK(buf[2] == '3');
  CHECK(buf[3] == '4');
  CHECK(buf[4] == '5');
  CHECK(buf[5] == '6');
  CHECK(buf[6] == '7');
  CHECK(buf[7] == '8');
}

TEST_CASE("regstr: constructor from string - empty string") {
  regstr r("");
  char buf[8];
  size_t len = r.extract(buf);

  CHECK(len == 0);
  CHECK(r.a == 0);
  CHECK(r.b == 0);
}

TEST_CASE("regstr: constructor from string - exceeds max length") {
  regstr r("123456789"); // 9 chars, exceeds limit
  char buf[8];
  size_t len = r.extract(buf);

  CHECK(len == 3);
  CHECK(strcmp(buf, "err") == 0);
}

TEST_CASE("regstr: round trip encoding/decoding") {
  const char *test_strings[] = {"hello", "world", "abc", "12345678", "x", ""};

  for (const char *str : test_strings) {
    regstr r(str);
    char buf[8];
    size_t len = r.extract(buf);

    CHECK(len == strlen(str));

    // For 8-char strings, use memcmp since there's no null terminator
    if (len == 8) {
      CHECK(memcmp(buf, str, 8) == 0);
    } else {
      CHECK(strcmp(buf, str) == 0);
    }
  }
}

TEST_CASE("regstr: case sensitivity") {
  regstr r1("hello");
  regstr r2("HELLO");

  CHECK(r1.a != r2.a); // Different encodings
}

TEST_CASE("regstr: values with different lengths") {
  regstr r1("ab");
  regstr r2("abcd");
  regstr r3("abcdefgh");

  char buf1[8], buf2[8], buf3[8];
  CHECK(r1.extract(buf1) == 2);
  CHECK(r2.extract(buf2) == 4);
  CHECK(r3.extract(buf3) == 8);

  CHECK(strcmp(buf1, "ab") == 0);
  CHECK(strcmp(buf2, "abcd") == 0);
  CHECK(buf3[0] == 'a');
  CHECK(buf3[7] == 'h');
}
