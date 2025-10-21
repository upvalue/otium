#include "vendor/doctest.h"
#include "ot/common.h"
#include "ot/shared/mpack-reader.hpp"
#include "ot/shared/mpack-writer.hpp"
#include <string.h>

TEST_CASE("string-view - equals") {
  StringView sv;
  sv.ptr = "hello";
  sv.len = 5;

  CHECK(sv.equals("hello"));
  CHECK_FALSE(sv.equals("hell"));
  CHECK_FALSE(sv.equals("hello!"));
  CHECK_FALSE(sv.equals("world"));
}

TEST_CASE("string-view - copy_to") {
  StringView sv;
  sv.ptr = "test";
  sv.len = 4;

  char buf[10];
  CHECK(sv.copy_to(buf, sizeof(buf)));
  CHECK(strcmp(buf, "test") == 0);

  // Too small buffer
  char small[3];
  CHECK_FALSE(sv.copy_to(small, sizeof(small)));
}

TEST_CASE("mpack-reader - read nil") {
  char buf[256];
  MPackWriter writer(buf, sizeof(buf));
  writer.nil();

  MPackReader reader(buf, writer.size());
  CHECK(reader.read_nil());
  CHECK(reader.ok());
}

TEST_CASE("mpack-reader - read bool") {
  char buf[256];
  MPackWriter writer(buf, sizeof(buf));
  writer.pack(true).pack(false);

  MPackReader reader(buf, writer.size());

  bool val1, val2;
  CHECK(reader.read_bool(val1));
  CHECK(val1 == true);

  CHECK(reader.read_bool(val2));
  CHECK(val2 == false);

  CHECK(reader.ok());
}

TEST_CASE("mpack-reader - read uint") {
  char buf[256];
  MPackWriter writer(buf, sizeof(buf));
  writer.pack((uint32_t)0).pack((uint32_t)42).pack((uint32_t)0xFFFFFFFF);

  MPackReader reader(buf, writer.size());

  uint32_t val1, val2, val3;
  CHECK(reader.read_uint(val1));
  CHECK(val1 == 0);

  CHECK(reader.read_uint(val2));
  CHECK(val2 == 42);

  CHECK(reader.read_uint(val3));
  CHECK(val3 == 0xFFFFFFFF);

  CHECK(reader.ok());
}

TEST_CASE("mpack-reader - read int") {
  char buf[256];
  MPackWriter writer(buf, sizeof(buf));
  writer.pack((int32_t)0).pack((int32_t)-17).pack((int32_t)123);

  MPackReader reader(buf, writer.size());

  int32_t val1, val2, val3;
  CHECK(reader.read_int(val1));
  CHECK(val1 == 0);

  CHECK(reader.read_int(val2));
  CHECK(val2 == -17);

  CHECK(reader.read_int(val3));
  CHECK(val3 == 123);

  CHECK(reader.ok());
}

TEST_CASE("mpack-reader - read string (zero-copy)") {
  char buf[256];
  MPackWriter writer(buf, sizeof(buf));
  writer.str("hello").str("world");

  MPackReader reader(buf, writer.size());

  StringView str1, str2;
  CHECK(reader.read_string(str1));
  CHECK(str1.len == 5);
  CHECK(str1.equals("hello"));

  CHECK(reader.read_string(str2));
  CHECK(str2.len == 5);
  CHECK(str2.equals("world"));

  CHECK(reader.ok());

  // Verify zero-copy: pointers should be inside buf
  CHECK(str1.ptr >= buf);
  CHECK(str1.ptr < buf + sizeof(buf));
  CHECK(str2.ptr >= buf);
  CHECK(str2.ptr < buf + sizeof(buf));
}

TEST_CASE("mpack-reader - enter array") {
  char buf[256];
  MPackWriter writer(buf, sizeof(buf));
  writer.array(3).pack((uint32_t)1).pack((uint32_t)2).pack((uint32_t)3);

  MPackReader reader(buf, writer.size());

  uint32_t count;
  CHECK(reader.enter_array(count));
  CHECK(count == 3);

  uint32_t val;
  CHECK(reader.read_uint(val));
  CHECK(val == 1);

  CHECK(reader.read_uint(val));
  CHECK(val == 2);

  CHECK(reader.read_uint(val));
  CHECK(val == 3);

  CHECK(reader.ok());
}

TEST_CASE("mpack-reader - enter map") {
  char buf[256];
  MPackWriter writer(buf, sizeof(buf));
  writer.map(2).str("a").pack((uint32_t)1).str("b").pack((uint32_t)2);

  MPackReader reader(buf, writer.size());

  uint32_t pairs;
  CHECK(reader.enter_map(pairs));
  CHECK(pairs == 2);

  StringView key;
  uint32_t val;

  CHECK(reader.read_string(key));
  CHECK(key.equals("a"));
  CHECK(reader.read_uint(val));
  CHECK(val == 1);

  CHECK(reader.read_string(key));
  CHECK(key.equals("b"));
  CHECK(reader.read_uint(val));
  CHECK(val == 2);

  CHECK(reader.ok());
}

TEST_CASE("mpack-reader - read stringarray") {
  char buf[256];
  MPackWriter writer(buf, sizeof(buf));

  char* args[] = {(char*)"cmd", (char*)"arg1", (char*)"arg2"};
  writer.stringarray(3, args);

  MPackReader reader(buf, writer.size());

  StringView views[10];
  size_t count;
  CHECK(reader.read_stringarray(views, 10, count));
  CHECK(count == 3);
  CHECK(views[0].equals("cmd"));
  CHECK(views[1].equals("arg1"));
  CHECK(views[2].equals("arg2"));

  CHECK(reader.ok());
}

TEST_CASE("mpack-reader - read args map") {
  char buf[256];
  MPackWriter writer(buf, sizeof(buf));

  char* args[] = {(char*)"program", (char*)"arg1", (char*)"arg2"};
  writer.map(1).str("args").stringarray(3, args);

  MPackReader reader(buf, writer.size());

  StringView argv_views[10];
  size_t argc;
  CHECK(reader.read_args_map(argv_views, 10, argc));
  CHECK(argc == 3);
  CHECK(argv_views[0].equals("program"));
  CHECK(argv_views[1].equals("arg1"));
  CHECK(argv_views[2].equals("arg2"));

  CHECK(reader.ok());
}

TEST_CASE("mpack-reader - args map with wrong key fails") {
  char buf[256];
  MPackWriter writer(buf, sizeof(buf));

  char* args[] = {(char*)"test"};
  writer.map(1).str("wrong").stringarray(1, args);

  MPackReader reader(buf, writer.size());

  StringView argv_views[10];
  size_t argc;
  CHECK_FALSE(reader.read_args_map(argv_views, 10, argc));
  CHECK_FALSE(reader.ok());
}

TEST_CASE("mpack-reader - stringarray buffer overflow") {
  char buf[256];
  MPackWriter writer(buf, sizeof(buf));

  char* args[] = {(char*)"a", (char*)"b", (char*)"c"};
  writer.stringarray(3, args);

  MPackReader reader(buf, writer.size());

  StringView views[2];  // Only space for 2
  size_t count;
  CHECK_FALSE(reader.read_stringarray(views, 2, count));
  CHECK_FALSE(reader.ok());
}

TEST_CASE("mpack-reader - type errors") {
  char buf[256];
  MPackWriter writer(buf, sizeof(buf));
  writer.pack((uint32_t)42);

  MPackReader reader(buf, writer.size());

  // Try to read uint as string
  StringView str;
  CHECK_FALSE(reader.read_string(str));
  CHECK_FALSE(reader.ok());
}
