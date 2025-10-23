// messages.hpp - common message types
#ifndef OT_SHARED_MESSAGES_HPP
#define OT_SHARED_MESSAGES_HPP

#include "ot/common.h"
#include "ot/shared/address.hpp"
#include "ot/shared/error-codes.hpp"
#include "ot/shared/mpack-reader.hpp"
#include "ot/shared/mpack-writer.hpp"
#include "ot/shared/string-view.hpp"

// Messages

// Messages always begin with a type
// Always an array

// Types
// error - type: 'error', code: string, message: string, <arbitrary contents
// based on type>
//   code => <source>.<name>
//     ex => kernel.invalid-pid
// string - type: 'string', <a single string>

struct MPackBuffer {
  char *buffer;
  size_t length;

  MPackBuffer(char *buffer, size_t length) : buffer(buffer), length(length) {}
  MPackBuffer(PageAddr page) : MPackBuffer(page.as<char>(), OT_PAGE_SIZE) {}
  ~MPackBuffer() {}
};

enum MsgSerializationError {
  MSG_SERIALIZATION_OK = 0,
  MSG_SERIALIZATION_EOF = 1,
  MSG_SERIALIZATION_UNEXPECTED_TYPE = 2,
  MSG_SERIALIZATION_OTHER = 3,
  MSG_SERIALIZATION_EXPECTED_TOPLEVEL_ARRAY = 4,
  MSG_SERIALIZATION_EXPECTED_TOPLEVEL_ARRAY_LEN = 5,
};

/** A message containing a single string */
struct MsgString : MPackBuffer {
  MsgString(char *buffer, size_t length) : MPackBuffer(buffer, length) {}
  MsgString(PageAddr page) : MPackBuffer(page) {}

  MsgSerializationError serialize(StringView &sv) {
    MPackWriter writer(buffer, length);
    writer.array(2).str("string").str(sv);
    return writer.ok() ? MSG_SERIALIZATION_OK : MSG_SERIALIZATION_OTHER;
  }

  MsgSerializationError deserialize(StringView &sv) {
    MPackReader reader(buffer, length);
    uint32_t count;
    if (!reader.enter_array(count)) {
      return MSG_SERIALIZATION_EXPECTED_TOPLEVEL_ARRAY;
    }
    if (count != 2) {
      return MSG_SERIALIZATION_EXPECTED_TOPLEVEL_ARRAY_LEN;
    }
    StringView type;
    if (!reader.read_string(type)) {
      return MSG_SERIALIZATION_UNEXPECTED_TYPE;
    }
    if (!type.equals("string")) {
      return MSG_SERIALIZATION_UNEXPECTED_TYPE;
    }
    if (!reader.read_string(sv)) {
      return MSG_SERIALIZATION_OTHER;
    }
    return reader.ok() ? MSG_SERIALIZATION_OK : MSG_SERIALIZATION_OTHER;
  }
};

struct MsgError : MPackBuffer {
  MsgError(char *buffer, size_t length) : MPackBuffer(buffer, length) {}
  MsgError(PageAddr page) : MPackBuffer(page) {}

  MsgSerializationError serialize(ErrorCode code, const char *fmt, ...) {
    // Format the user message
    va_list args;
    va_start(args, fmt);
    ovsnprintf(ot_scratch_buffer, OT_PAGE_SIZE, fmt, args);
    va_end(args);

    // Prepend error code string
    char final_msg[OT_PAGE_SIZE];
    osnprintf(final_msg, OT_PAGE_SIZE, "%s: %s", error_code_to_string(code),
              ot_scratch_buffer);

    // Serialize as array
    MPackWriter writer(buffer, length);
    writer.array(3).str("error").pack(code).str(final_msg);
    return writer.ok() ? MSG_SERIALIZATION_OK : MSG_SERIALIZATION_OTHER;
  }

  MsgSerializationError deserialize(ErrorCode &code, StringView &message) {
    MPackReader reader(buffer, length);
    uint32_t count;

    if (!reader.enter_array(count)) {
      return MSG_SERIALIZATION_EXPECTED_TOPLEVEL_ARRAY;
    }

    if (count != 3) {
      return MSG_SERIALIZATION_EXPECTED_TOPLEVEL_ARRAY_LEN;
    }

    StringView type;

    if (!reader.read_string(type)) {
      return MSG_SERIALIZATION_UNEXPECTED_TYPE;
    }

    if (!type.equals("error")) {
      return MSG_SERIALIZATION_UNEXPECTED_TYPE;
    }

    if (!reader.read_error_code(code)) {
      return MSG_SERIALIZATION_OTHER;
    }

    if (!reader.read_string(message)) {
      return MSG_SERIALIZATION_OTHER;
    }

    return reader.ok() ? MSG_SERIALIZATION_OK : MSG_SERIALIZATION_OTHER;
  }
};
#endif