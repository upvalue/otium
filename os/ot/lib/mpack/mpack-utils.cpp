#include "ot/lib/mpack/mpack-utils.hpp"
#include "ot/common.h"

// Context for pretty printing
struct mpack_print_ctx {
  mpack_putchar_fn putchar_fn;
  int error; // Set to 1 if putchar returns 0
};

// Helper to write a character
static void write_char(mpack_print_ctx *ctx, char ch) {
  if (ctx->error)
    return;
  if (!ctx->putchar_fn(ch)) {
    ctx->error = 1;
  }
}

// Helper to write a string
static void write_str(mpack_print_ctx *ctx, const char *s) {
  while (*s && !ctx->error) {
    write_char(ctx, *s++);
  }
}

// Helper to write an escaped character (for string content)
static void write_escaped_char(mpack_print_ctx *ctx, char ch) {
  switch (ch) {
  case '\n':
    write_char(ctx, '\\');
    write_char(ctx, 'n');
    break;
  case '\r':
    write_char(ctx, '\\');
    write_char(ctx, 'r');
    break;
  default:
    write_char(ctx, ch);
    break;
  }
}

// Helper to write a uint32_t
static void write_uint32(mpack_print_ctx *ctx, uint32_t val) {
  if (val == 0) {
    write_char(ctx, '0');
    return;
  }

  char buf[12]; // Max uint32_t is 10 digits + null + sign
  int pos = 0;

  while (val > 0 && pos < 11) {
    buf[pos++] = '0' + (val % 10);
    val /= 10;
  }

  // Reverse the string
  for (int i = pos - 1; i >= 0; i--) {
    write_char(ctx, buf[i]);
  }
}

// Helper to write an int32_t
static void write_int32(mpack_print_ctx *ctx, int32_t val) {
  if (val < 0) {
    write_char(ctx, '-');
    // Handle INT32_MIN specially
    if (val == -2147483647 - 1) {
      write_str(ctx, "2147483648");
      return;
    }
    val = -val;
  }
  write_uint32(ctx, (uint32_t)val);
}

// Callback when entering a node
static void print_enter(mpack_parser_t *parser, mpack_node_t *node) {
  mpack_print_ctx *ctx = (mpack_print_ctx *)parser->data.p;
  mpack_token_t *tok = &node->tok;

  if (ctx->error)
    return;

  switch (tok->type) {
  case MPACK_TOKEN_NIL:
    write_str(ctx, "null");
    break;

  case MPACK_TOKEN_BOOLEAN:
    write_str(ctx, mpack_unpack_boolean(*tok) ? "true" : "false");
    break;

  case MPACK_TOKEN_UINT:
    // For uint, we need to handle both 32 and 64 bit
    if (tok->data.value.hi == 0) {
      write_uint32(ctx, tok->data.value.lo);
    } else {
      // For 64-bit, just write the low part with indication it's truncated
      write_uint32(ctx, tok->data.value.lo);
      write_str(ctx, "...");
    }
    break;

  case MPACK_TOKEN_SINT: {
    // Use mpack_unpack_sint for proper sign handling
    mpack_sintmax_t val = mpack_unpack_sint(*tok);
    // For simplicity, only handle 32-bit range properly
    if (val >= -2147483648LL && val <= 2147483647LL) {
      write_int32(ctx, (int32_t)val);
    } else {
      // Out of 32-bit range, show truncated value
      write_int32(ctx, (int32_t)val);
      write_str(ctx, "...");
    }
    break;
  }

  case MPACK_TOKEN_CHUNK: {
    // Only write chunk data for strings, not for bin/ext
    mpack_node_t *parent = MPACK_PARENT_NODE(node);
    if (parent && (parent->tok.type == MPACK_TOKEN_BIN ||
                   parent->tok.type == MPACK_TOKEN_EXT)) {
      // Skip binary/ext data chunks
      break;
    }
    // Write string data with escaping
    for (uint32_t i = 0; i < tok->length && !ctx->error; i++) {
      write_escaped_char(ctx, tok->data.chunk_ptr[i]);
    }
    break;
  }

  case MPACK_TOKEN_STR:
    write_char(ctx, '"');
    break;

  case MPACK_TOKEN_BIN:
    write_str(ctx, "<bin:");
    write_uint32(ctx, tok->length);
    write_char(ctx, '>');
    break;

  case MPACK_TOKEN_EXT:
    write_str(ctx, "<ext:");
    write_uint32(ctx, tok->data.ext_type);
    write_char(ctx, ':');
    write_uint32(ctx, tok->length);
    write_char(ctx, '>');
    break;

  case MPACK_TOKEN_ARRAY:
    write_char(ctx, '[');
    break;

  case MPACK_TOKEN_MAP:
    write_char(ctx, '{');
    break;

  default:
    break;
  }
}

// Callback when exiting a node
static void print_exit(mpack_parser_t *parser, mpack_node_t *node) {
  mpack_print_ctx *ctx = (mpack_print_ctx *)parser->data.p;
  mpack_token_t *tok = &node->tok;
  mpack_node_t *parent = MPACK_PARENT_NODE(node);

  if (ctx->error)
    return;

  switch (tok->type) {
  case MPACK_TOKEN_STR:
    write_char(ctx, '"');
    break;

  case MPACK_TOKEN_ARRAY:
    write_char(ctx, ']');
    break;

  case MPACK_TOKEN_MAP:
    write_char(ctx, '}');
    break;

  default:
    break;
  }

  // Add comma or colon separators
  if (parent && parent->tok.type <= MPACK_TOKEN_MAP) {
    if (parent->pos < parent->tok.length) {
      if (parent->tok.type == MPACK_TOKEN_MAP) {
        // In maps, alternate between : and ,
        write_char(ctx, parent->key_visited ? ':' : ',');
      } else {
        // In arrays, always use comma
        write_char(ctx, ',');
      }
    }
  }
}

int mpack_print(const char *data, size_t len, mpack_putchar_fn putchar_fn) {
  if (!data || !putchar_fn)
    return 0;

  mpack_print_ctx ctx;
  ctx.putchar_fn = putchar_fn;
  ctx.error = 0;

  mpack_parser_t parser;
  mpack_parser_init(&parser, 0);
  parser.data.p = &ctx;

  const char *buf = data;
  size_t buflen = len;

  int result = mpack_parse(&parser, &buf, &buflen, print_enter, print_exit);

  if (result != MPACK_OK || ctx.error) {
    return 0;
  }

  return 1;
}

// Context for buffer printing
struct mpack_sprint_ctx {
  char *buf;
  size_t size;
  size_t pos;
};

static mpack_sprint_ctx g_sprint_ctx;

static int sprint_putchar(char ch) {
  if (g_sprint_ctx.pos < g_sprint_ctx.size - 1) {
    g_sprint_ctx.buf[g_sprint_ctx.pos++] = ch;
    return 1;
  }
  return 0;
}

int mpack_sprint(const char *data, size_t data_len, char *buf, size_t buf_size) {
  if (!data || !buf || buf_size == 0)
    return -1;

  g_sprint_ctx.buf = buf;
  g_sprint_ctx.size = buf_size;
  g_sprint_ctx.pos = 0;

  int result = mpack_print(data, data_len, sprint_putchar);

  // Null terminate
  if (g_sprint_ctx.pos < buf_size) {
    buf[g_sprint_ctx.pos] = '\0';
  } else {
    buf[buf_size - 1] = '\0';
  }

  return result ? (int)g_sprint_ctx.pos : -1;
}

#ifndef OT_POSIX
int mpack_oprint(const char *data, size_t len) {
  return mpack_print(data, len, [](char ch) -> int { return oputchar(ch); });
}

int mpack_scratch_print(const char *data, size_t data_len) {
  return mpack_sprint(data, data_len, ot_scratch_buffer, OT_PAGE_SIZE);
}
#endif
