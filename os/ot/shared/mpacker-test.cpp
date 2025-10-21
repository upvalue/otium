#include "vendor/doctest.h"
#include "ot/common.h"
#include "ot/shared/mpacker.hpp"
#include "ot/shared/mpack.h"

TEST_CASE("mpacker - basic types") {
  char buf[256];
  MPacker msg(buf, sizeof(buf));

  // Pack some basic values
  msg.nil();
  msg.pack(true);
  msg.pack(false);
  msg.pack((uint32_t)42);
  msg.pack((int32_t)-17);

  CHECK(msg.ok());
  CHECK(msg.size() > 0);
}

TEST_CASE("mpacker - strings") {
  char buf[256];
  MPacker msg(buf, sizeof(buf));

  msg.str("hello");
  msg.str("world", 5);

  CHECK(msg.ok());
  CHECK(msg.size() > 0);

  // Unpack and verify
  const char* rbuf = (const char*)msg.data();
  size_t rlen = msg.size();
  mpack_tokbuf_t state;
  mpack_tokbuf_init(&state);
  mpack_token_t tok;

  int result = mpack_read(&state, &rbuf, &rlen, &tok);
  CHECK(result == MPACK_OK);
  CHECK(tok.type == MPACK_TOKEN_STR);
  CHECK(tok.length == 5);
}

TEST_CASE("mpacker - stringarray") {
  char buf[256];
  MPacker msg(buf, sizeof(buf));

  char* args[] = {
    (char*)"cmd",
    (char*)"arg1",
    (char*)"arg2",
    (char*)"arg3"
  };

  msg.stringarray(4, args);

  CHECK(msg.ok());
  CHECK(msg.size() > 0);

  // Unpack and verify the array
  const char* rbuf = (const char*)msg.data();
  size_t rlen = msg.size();
  mpack_tokbuf_t state;
  mpack_tokbuf_init(&state);
  mpack_token_t tok;

  int result = mpack_read(&state, &rbuf, &rlen, &tok);
  CHECK(result == MPACK_OK);
  CHECK(tok.type == MPACK_TOKEN_ARRAY);
  CHECK(tok.length == 4);

  // Read first string
  result = mpack_read(&state, &rbuf, &rlen, &tok);
  CHECK(result == MPACK_OK);
  CHECK(tok.type == MPACK_TOKEN_STR);
  CHECK(tok.length == 3);

  // Read the chunk
  result = mpack_read(&state, &rbuf, &rlen, &tok);
  CHECK(result == MPACK_OK);
  CHECK(tok.type == MPACK_TOKEN_CHUNK);
  CHECK(memcmp(tok.data.chunk_ptr, "cmd", 3) == 0);
}

TEST_CASE("mpacker - collections") {
  char buf[256];
  MPacker msg(buf, sizeof(buf));

  // Pack: [1, 2, 3]
  msg.array(3)
     .pack((uint32_t)1)
     .pack((uint32_t)2)
     .pack((uint32_t)3);

  CHECK(msg.ok());

  // Pack: {"key": "value"}
  msg.reset();
  msg.map(1)
     .str("key")
     .str("value");

  CHECK(msg.ok());
  CHECK(msg.size() > 0);
}

TEST_CASE("mpacker - binary") {
  char buf[256];
  MPacker msg(buf, sizeof(buf));

  uint8_t data[] = {0xde, 0xad, 0xbe, 0xef};
  msg.bin(data, 4);

  CHECK(msg.ok());
  CHECK(msg.size() > 0);

  // Unpack and verify
  const char* rbuf = (const char*)msg.data();
  size_t rlen = msg.size();
  mpack_tokbuf_t state;
  mpack_tokbuf_init(&state);
  mpack_token_t tok;

  int result = mpack_read(&state, &rbuf, &rlen, &tok);
  CHECK(result == MPACK_OK);
  CHECK(tok.type == MPACK_TOKEN_BIN);
  CHECK(tok.length == 4);
}

TEST_CASE("mpacker - reset") {
  char buf[256];
  MPacker msg(buf, sizeof(buf));

  msg.pack((uint32_t)42);
  uint32_t size1 = msg.size();
  CHECK(size1 > 0);

  msg.reset();
  CHECK(msg.size() == 0);

  msg.pack((uint32_t)99);
  uint32_t size2 = msg.size();

  // Both values are positive fixints, so same encoding size
  CHECK(size1 == size2);
  CHECK(msg.ok());
}
