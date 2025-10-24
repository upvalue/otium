// printf-test.cpp - test mpaland/printf integration
#include "ot/common.h"
#include "vendor/doctest.h"

TEST_CASE("printf - basic formatting") {
  char buf[256];

  SUBCASE("%u unsigned") {
    osnprintf(buf, sizeof(buf), "%u", 42u);
    CHECK(strcmp(buf, "42") == 0);
  }

  SUBCASE("%u large unsigned") {
    osnprintf(buf, sizeof(buf), "%u", 4294967295u);
    CHECK(strcmp(buf, "4294967295") == 0);
  }

  SUBCASE("%d negative") {
    osnprintf(buf, sizeof(buf), "%d", -5);
    CHECK(strcmp(buf, "-5") == 0);
  }

  SUBCASE("%02x zero-padded hex") {
    osnprintf(buf, sizeof(buf), "%02x", 0xfb);
    CHECK(strcmp(buf, "fb") == 0);
  }

  SUBCASE("%04x zero-padded hex") {
    osnprintf(buf, sizeof(buf), "%04x", 0x12ab);
    CHECK(strcmp(buf, "12ab") == 0);
  }

  SUBCASE("%p pointer") {
    osnprintf(buf, sizeof(buf), "%p", (void*)0x1234);
    // Just check it doesn't crash and has some output
    CHECK(buf[0] != '\0');
  }

  SUBCASE("%5d width") {
    osnprintf(buf, sizeof(buf), "%5d", 42);
    CHECK(strcmp(buf, "   42") == 0);
  }

  SUBCASE("%-5d left-aligned") {
    osnprintf(buf, sizeof(buf), "%-5d", 42);
    CHECK(strcmp(buf, "42   ") == 0);
  }

  SUBCASE("combined format") {
    osnprintf(buf, sizeof(buf), "val=%u hex=%02x", 255, 0xff);
    CHECK(strcmp(buf, "val=255 hex=ff") == 0);
  }
}

TEST_CASE("printf - edge cases") {
  char buf[256];

  SUBCASE("buffer truncation") {
    char small[5];
    int written = osnprintf(small, sizeof(small), "hello world");
    CHECK(strcmp(small, "hell") == 0);
    CHECK(written > 4); // Should return would-be length
  }

  SUBCASE("empty format") {
    osnprintf(buf, sizeof(buf), "");
    CHECK(strcmp(buf, "") == 0);
  }

  SUBCASE("just text") {
    osnprintf(buf, sizeof(buf), "hello");
    CHECK(strcmp(buf, "hello") == 0);
  }
}
