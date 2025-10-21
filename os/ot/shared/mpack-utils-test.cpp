#include "vendor/doctest.h"
#include "ot/common.h"
#include "ot/shared/mpack-utils.hpp"
#include "ot/shared/mpack-writer.hpp"
#include <string.h>

// Test buffer for capturing output
static char test_output[1024];
static size_t test_output_pos;

// Reset test buffer
static void reset_output() {
  memset(test_output, 0, sizeof(test_output));
  test_output_pos = 0;
}

// Test putchar that writes to buffer
static int test_putchar(char ch) {
  if (test_output_pos >= sizeof(test_output) - 1) {
    return 0;  // Buffer full
  }
  test_output[test_output_pos++] = ch;
  return 1;
}

// Test putchar that always fails
static int fail_putchar(char ch) {
  (void)ch;
  return 0;
}

TEST_CASE("mpack-utils - print basic types") {
  char buf[256];
  MPackWriter msg(buf, sizeof(buf));

  // Pack: [null, true, false, 42, -17]
  msg.array(5)
     .nil()
     .pack(true)
     .pack(false)
     .pack((uint32_t)42)
     .pack((int32_t)-17);

  CHECK(msg.ok());

  reset_output();
  int result = mpack_print((const char*)msg.data(), msg.size(), test_putchar);

  CHECK(result == 1);
  INFO("Expected: [null,true,false,42,-17]");
  INFO("Got: " << test_output);
  CHECK(strcmp(test_output, "[null,true,false,42,-17]") == 0);
}

TEST_CASE("mpack-utils - print strings") {
  char buf[256];
  MPackWriter msg(buf, sizeof(buf));

  // Pack: ["hello", "world"]
  msg.array(2)
     .str("hello")
     .str("world");

  CHECK(msg.ok());

  reset_output();
  int result = mpack_print((const char*)msg.data(), msg.size(), test_putchar);

  CHECK(result == 1);
  CHECK(strcmp(test_output, "[\"hello\",\"world\"]") == 0);
}

TEST_CASE("mpack-utils - print nested arrays") {
  char buf[256];
  MPackWriter msg(buf, sizeof(buf));

  // Pack: [[1, 2], [3, 4]]
  msg.array(2)
     .array(2).pack((uint32_t)1).pack((uint32_t)2)
     .array(2).pack((uint32_t)3).pack((uint32_t)4);

  CHECK(msg.ok());

  reset_output();
  int result = mpack_print((const char*)msg.data(), msg.size(), test_putchar);

  CHECK(result == 1);
  CHECK(strcmp(test_output, "[[1,2],[3,4]]") == 0);
}

TEST_CASE("mpack-utils - print map") {
  char buf[256];
  MPackWriter msg(buf, sizeof(buf));

  // Pack: {"name": "alice", "age": 30}
  msg.map(2)
     .str("name").str("alice")
     .str("age").pack((uint32_t)30);

  CHECK(msg.ok());

  reset_output();
  int result = mpack_print((const char*)msg.data(), msg.size(), test_putchar);

  CHECK(result == 1);
  CHECK(strcmp(test_output, "{\"name\":\"alice\",\"age\":30}") == 0);
}

TEST_CASE("mpack-utils - print binary") {
  char buf[256];
  MPackWriter msg(buf, sizeof(buf));

  uint8_t data[] = {0xde, 0xad, 0xbe, 0xef};
  msg.bin(data, 4);

  CHECK(msg.ok());

  reset_output();
  int result = mpack_print((const char*)msg.data(), msg.size(), test_putchar);

  CHECK(result == 1);
  INFO("Expected: <bin:4>");
  INFO("Got: " << test_output);
  // Binary is printed as <bin:length>
  CHECK(strcmp(test_output, "<bin:4>") == 0);
}

TEST_CASE("mpack-utils - print stringarray") {
  char buf[256];
  MPackWriter msg(buf, sizeof(buf));

  char* args[] = {(char*)"cmd", (char*)"arg1", (char*)"arg2"};
  msg.stringarray(3, args);

  CHECK(msg.ok());

  reset_output();
  int result = mpack_print((const char*)msg.data(), msg.size(), test_putchar);

  CHECK(result == 1);
  CHECK(strcmp(test_output, "[\"cmd\",\"arg1\",\"arg2\"]") == 0);
}

TEST_CASE("mpack-utils - complex nested structure") {
  char buf[512];
  MPackWriter msg(buf, sizeof(buf));

  // Pack: {"users": [{"name": "alice", "age": 30}, {"name": "bob", "age": 25}], "count": 2}
  msg.map(2)
     .str("users").array(2)
       .map(2).str("name").str("alice").str("age").pack((uint32_t)30)
       .map(2).str("name").str("bob").str("age").pack((uint32_t)25)
     .str("count").pack((uint32_t)2);

  CHECK(msg.ok());

  reset_output();
  int result = mpack_print((const char*)msg.data(), msg.size(), test_putchar);

  CHECK(result == 1);
  CHECK(strcmp(test_output,
    "{\"users\":[{\"name\":\"alice\",\"age\":30},{\"name\":\"bob\",\"age\":25}],\"count\":2}") == 0);
}

TEST_CASE("mpack-utils - handles putchar failure") {
  char buf[256];
  MPackWriter msg(buf, sizeof(buf));
  msg.array(1).pack((uint32_t)42);

  CHECK(msg.ok());

  // Use a putchar that always fails
  int result = mpack_print((const char*)msg.data(), msg.size(), fail_putchar);

  CHECK(result == 0);
}

TEST_CASE("mpack-utils - handles null inputs") {
  CHECK(mpack_print(nullptr, 100, test_putchar) == 0);
  CHECK(mpack_print("data", 4, nullptr) == 0);
}

TEST_CASE("mpack-utils - empty array") {
  char buf[256];
  MPackWriter msg(buf, sizeof(buf));
  msg.array(0);

  CHECK(msg.ok());

  reset_output();
  int result = mpack_print((const char*)msg.data(), msg.size(), test_putchar);

  CHECK(result == 1);
  CHECK(strcmp(test_output, "[]") == 0);
}

TEST_CASE("mpack-utils - empty map") {
  char buf[256];
  MPackWriter msg(buf, sizeof(buf));
  msg.map(0);

  CHECK(msg.ok());

  reset_output();
  int result = mpack_print((const char*)msg.data(), msg.size(), test_putchar);

  CHECK(result == 1);
  CHECK(strcmp(test_output, "{}") == 0);
}
