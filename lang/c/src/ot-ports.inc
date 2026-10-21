/* Buffered blocking ports. Managed bytevectors keep storage movable between
 * calls; a host read/write cannot enter Otium or trigger its collector. */
#define as_port(V) ((ot_port_obj*)ot_as_obj(V))

enum {
  PORT_INPUT_MEMORY = 1,
  PORT_STRING,
  PORT_BYTES,
  PORT_FILE,
  PORT_CUSTOM,
  PORT_WRITER,
  PORT_STANDARD
};

void ot_set_io_host(ots* state, const ot_io_host* host, void* userdata) {
  state->io_host = host ? *host : *ot_platform_io_host();
  state->io_userdata = userdata;
}

static otv port_error(ots* s, otv p, const char* op, int reason) {
  static const char* reasons[] = {"other",        "not-found",     "exists",   "permission",
                                  "is-directory", "not-directory", "no-space", "closed",
                                  "other",        "other"};
  if (reason < 0 || reason > OT_IO_INTERRUPTED) reason = OT_IO_OTHER;
  OT_FRAME_SCOPED(s, &p);
  otv condition = ot_table_new(s, 6), key = ot_nil, value = ot_nil;
  OT_FRAME_SCOPED(s, &condition, &key, &value);
  const char* fields[] = {"type", "op", "reason", "message", "path", "detail"};
  for (size_t i = 0; i < 6; i++) {
    key = ot_intern(s, fields[i], strlen(fields[i]), true);
    if (i == 0)
      value = ot_intern(s, reason == OT_IO_CLOSED ? "io-error" : "file-error",
                        reason == OT_IO_CLOSED ? 8 : 10, false);
    else if (i == 1) value = ot_intern(s, op, strlen(op), false);
    else if (i == 2) value = ot_intern(s, reasons[reason], strlen(reasons[reason]), true);
    else if (i == 4) value = is_type(p, OBJ_PORT) ? as_port(p)->name : p;
    else value = ot_make_string(s, reasons[reason], strlen(reasons[reason]));
    ot_table_put(s, condition, key, value);
  }
  return ot_raise_value(s, condition);
}

static otv port_check(ots* s, otv p, bool input, const char* op) {
  if (!is_type(p, OBJ_PORT)) return ot_raise(s, "%s: expected port", op);
  if (as_port(p)->closed) return port_error(s, p, op, OT_IO_CLOSED);
  if (as_port(p)->input != input) return ot_raise(s, "%s: wrong port direction", op);
  return ot_nil;
}

static otv port_callback(ots* s, otv p, const char* name) {
  OT_FRAME_SCOPED(s, &p);
  otv key = ot_intern(s, name, strlen(name), true);
  return ot_table_get(s, as_port(p)->callbacks, key, ot_nil);
}

static otv port_new(ots* s, otv name, bool input, unsigned kind, intptr_t handle, otv buffer,
                    otv callbacks) {
  OT_FRAME_SCOPED(s, &name, &buffer, &callbacks);
  ot_port_obj* p = must_alloc(s, sizeof(*p), OBJ_PORT);
  ot_store(s, p, &p->name, name);
  ot_store(s, p, &p->buffer, buffer);
  ot_store(s, p, &p->callbacks, callbacks);
  p->input = input;
  p->kind = kind;
  p->handle = handle;
  p->at = p->position = 0;
  p->closed = false;
  p->eof = kind == PORT_INPUT_MEMORY;
  p->end = p->eof ? as_bytevector(buffer)->length : 0;
  otv value = ot_from_obj(p);
  OT_FRAME_SCOPED(s, &value);
  if (kind == PORT_FILE || kind == PORT_CUSTOM) s->open_ports = ot_cons(s, value, s->open_ports);
  return value;
}

static otv port_flush(ots* s, otv value) {
  if (port_check(s, value, false, "flush-output-port") == OT_UNWIND) return OT_UNWIND;
  OT_FRAME_SCOPED(s, &value);
  unsigned kind = as_port(value)->kind;
  if (kind == PORT_STRING || kind == PORT_BYTES) return ot_nil;
  while (as_port(value)->end) {
    ot_port_obj* p = as_port(value);
    size_t end = p->end;
    long count;
    if (kind == PORT_WRITER) {
      s->writer(s->writer_userdata, (char*)as_bytevector(p->buffer)->data, end);
      count = (long)end;
    } else if (kind == PORT_CUSTOM) {
      otv callback = port_callback(s, value, "write");
      otv args[] = {as_port(value)->buffer, ot_make_int(0), ot_make_int((intptr_t)end)};
      otv result = apply_value(s, callback, args, 3, false);
      if (result == OT_UNWIND) return result;
      if (!ot_is_int(result) || ot_get_int(result) <= 0 || (size_t)ot_get_int(result) > end)
        return ot_raise(s, "write-bytevector: invalid custom write count");
      count = ot_get_int(result);
    } else {
      count = s->io_host.write
                  ? s->io_host.write(s->io_userdata, p->handle, as_bytevector(p->buffer)->data, end)
                  : -OT_IO_OTHER;
      if (count == -OT_IO_INTERRUPTED) {
        if (atomic_exchange(&s->vm.interrupted, false) && run_interrupt_hook(s) == OT_UNWIND)
          return OT_UNWIND;
        continue;
      }
      if (count < 0) return port_error(s, value, "write-bytevector", (int)-count);
      if (count == 0 || (size_t)count > end)
        return ot_raise(s, "write-bytevector: invalid host write count");
    }
    p = as_port(value);
    memmove(as_bytevector(p->buffer)->data, as_bytevector(p->buffer)->data + count,
            end - (size_t)count);
    p->end -= (size_t)count;
  }
  if (kind == PORT_WRITER && s->writer == ot_default_write)
    fflush(s->writer_userdata ? (FILE*)s->writer_userdata : stdout);
  if (kind == PORT_CUSTOM) {
    otv callback = port_callback(s, value, "flush");
    if (!is_falsy(callback) && apply_value(s, callback, NULL, 0, false) == OT_UNWIND)
      return OT_UNWIND;
  }
  return ot_nil;
}

static otv port_ensure(ots* s, otv value, size_t needed) {
  if (port_check(s, value, true, "read") == OT_UNWIND) return OT_UNWIND;
  OT_FRAME_SCOPED(s, &value);
  if (as_port(value)->kind == PORT_STANDARD && as_port(value)->handle == 0) {
    otv output = ((ot_param_obj*)ot_as_obj(s->output_port_param))->value;
    if (!as_port(output)->closed && port_flush(s, output) == OT_UNWIND) return OT_UNWIND;
  }
  for (;;) {
    ot_port_obj* p = as_port(value);
    if (p->end - p->at >= needed || p->eof) return ot_nil;
    ot_bytevector_obj* buffer = as_bytevector(p->buffer);
    memmove(buffer->data, buffer->data + p->at, p->end - p->at);
    p->end -= p->at;
    p->at = 0;
    size_t end = p->end, capacity = buffer->length;
    long count;
    if (p->kind == PORT_CUSTOM) {
      otv callback = port_callback(s, value, "read");
      otv args[] = {as_port(value)->buffer, ot_make_int((intptr_t)end),
                    ot_make_int((intptr_t)capacity)};
      otv result = apply_value(s, callback, args, 3, false);
      if (result == OT_UNWIND) return result;
      if (result == ot_eof) count = 0;
      else if (ot_is_int(result) && ot_get_int(result) > 0 &&
               (size_t)ot_get_int(result) <= capacity - end)
        count = ot_get_int(result);
      else return ot_raise(s, "read-bytevector: invalid custom read count");
    } else {
      if (atomic_exchange(&s->vm.interrupted, false) && run_interrupt_hook(s) == OT_UNWIND)
        return OT_UNWIND;
      p = as_port(value);
      buffer = as_bytevector(p->buffer);
      count = s->io_host.read
                  ? s->io_host.read(s->io_userdata, p->handle, buffer->data + end, capacity - end)
                  : -OT_IO_OTHER;
      if (count == -OT_IO_INTERRUPTED) {
        if (atomic_exchange(&s->vm.interrupted, false) && run_interrupt_hook(s) == OT_UNWIND)
          return OT_UNWIND;
        continue;
      }
      if (count < 0) return port_error(s, value, "read-bytevector", (int)-count);
      if ((size_t)count > capacity - end)
        return ot_raise(s, "read-bytevector: invalid host read count");
    }
    p = as_port(value);
    if (!count) p->eof = true;
    p->end += (size_t)count;
  }
}

static otv port_peek(ots* s, otv value, bool character, bool consume) {
  OT_FRAME_SCOPED(s, &value);
  if (port_ensure(s, value, 1) == OT_UNWIND) return OT_UNWIND;
  ot_port_obj* p = as_port(value);
  if (p->at == p->end) return ot_eof;
  unsigned char byte = as_bytevector(p->buffer)->data[p->at];
  size_t width = 1;
  otv result = ot_make_int(byte);
  if (character) {
    width = byte < 128                     ? 1
            : byte >= 0xc2 && byte <= 0xdf ? 2
            : byte >= 0xe0 && byte <= 0xef ? 3
            : byte >= 0xf0 && byte <= 0xf4 ? 4
                                           : 1;
    if (port_ensure(s, value, width) == OT_UNWIND) return OT_UNWIND;
    p = as_port(value);
    const unsigned char* data = as_bytevector(p->buffer)->data + p->at;
    if (!utf8_valid_sequence(data, p->end - p->at))
      return io_error(s, "decode-error", "read-char", "read-char: invalid UTF-8", "position",
                      ot_make_int((intptr_t)p->position));
    result = ot_make_char(utf8_decode(data, width, 0));
  }
  p = as_port(value);
  if (consume) {
    p->at += width;
    p->position += width;
  }
  return result;
}

static otv port_current(ots* s, bool input) {
  otv param = input ? s->input_port_param : s->output_port_param;
  return apply_value(s, param, NULL, 0, false);
}

static otv port_optional(ots* s, otv* args, int argc, int index, bool input) {
  return argc > index ? args[index] : port_current(s, input);
}

static otv port_write_bytes(ots* s, otv value, otv bytes, size_t start, size_t end) {
  if (port_check(s, value, false, "write-bytevector") == OT_UNWIND) return OT_UNWIND;
  OT_FRAME_SCOPED(s, &value, &bytes);
  while (start < end) {
    ot_port_obj* p = as_port(value);
    size_t capacity = as_bytevector(p->buffer)->length;
    if (p->end == capacity) {
      if (p->kind == PORT_STRING || p->kind == PORT_BYTES) {
        otv grown = make_bytevector(s, capacity * 2);
        p = as_port(value);
        memcpy(as_bytevector(grown)->data, as_bytevector(p->buffer)->data, p->end);
        ot_store(s, p, &p->buffer, grown);
      } else if (port_flush(s, value) == OT_UNWIND) return OT_UNWIND;
      continue;
    }
    size_t count = end - start;
    if (count > capacity - p->end) count = capacity - p->end;
    memcpy(as_bytevector(p->buffer)->data + p->end, as_bytevector(bytes)->data + start, count);
    p->end += count;
    start += count;
  }
  if (as_port(value)->kind == PORT_WRITER || as_port(value)->kind == PORT_STANDARD)
    return port_flush(s, value);
  return ot_nil;
}

static otv port_output(ots* s, otv p, otv* args, int argc, bool display, bool newline) {
  OT_FRAME_SCOPED(s, &p);
  buf text = {0};
  for (int i = 0; i < argc; i++) {
    if (i) buf_byte(&text, ' ');
    render_value(s, &text, args[i], display, 0);
  }
  if (newline) buf_byte(&text, '\n');
  if (text.failed) {
    buf_free(&text);
    return ot_raise(s, "output: out of memory");
  }
  otv bytes = make_bytevector(s, text.length);
  memcpy(as_bytevector(bytes)->data, text.data, text.length);
  buf_free(&text);
  return port_write_bytes(s, p, bytes, 0, as_bytevector(bytes)->length);
}

static otv n_display_to(ots* s, otv* a, int n) {
  if (n < 1) return ot_raise(s, "display-to: expected port");
  return port_output(s, a[0], a + 1, n - 1, true, false);
}

static otv n_write_to(ots* s, otv* a, int n) {
  if (n < 1) return ot_raise(s, "write-to: expected port");
  return port_output(s, a[0], a + 1, n - 1, false, false);
}

static otv n_port_p(ots* s, otv* a, int n) {
  if (need_arity(s, "port?", n, 1, 1) == OT_UNWIND) return OT_UNWIND;
  return (is_type(a[0], OBJ_PORT) ? ot_true : ot_false);
}

#define PORT_PRED(NAME, EXPR)                                                                      \
  static otv NAME(ots* s, otv* a, int n) {                                                         \
    if (need_arity(s, #NAME, n, 1, 1) == OT_UNWIND) return OT_UNWIND;                              \
    return (is_type(a[0], OBJ_PORT) && (EXPR) ? ot_true : ot_false);                               \
  }
PORT_PRED(n_input_p, as_port(a[0])->input)
PORT_PRED(n_output_p, !as_port(a[0])->input)
PORT_PRED(n_input_open_p, as_port(a[0])->input && !as_port(a[0])->closed)
PORT_PRED(n_output_open_p, !as_port(a[0])->input && !as_port(a[0])->closed)
#undef PORT_PRED
#define PORT_READ(NAME, CHAR, CONSUME)                                                             \
  static otv NAME(ots* s, otv* a, int n) {                                                         \
    if (need_arity(s, #NAME, n, 0, 1) == OT_UNWIND) return OT_UNWIND;                              \
    return port_peek(s, port_optional(s, a, n, 0, true), CHAR, CONSUME);                           \
  }
PORT_READ(n_read_char, true, true)
PORT_READ(n_peek_char, true, false)
PORT_READ(n_read_u8, false, true)
PORT_READ(n_peek_u8, false, false)
#undef PORT_READ

static otv n_flush(ots* s, otv* a, int n) {
  if (need_arity(s, "flush-output-port", n, 0, 1) == OT_UNWIND) return OT_UNWIND;
  return port_flush(s, port_optional(s, a, n, 0, false));
}

static otv port_close(ots* s, otv value, int direction) {
  if (!is_type(value, OBJ_PORT)) return ot_raise(s, "close-port: expected port");
  if (direction && as_port(value)->input != (direction == 1))
    return ot_raise(s, "close-port: wrong port direction");
  if (as_port(value)->closed) return ot_nil;
  OT_FRAME_SCOPED(s, &value);
  if (!as_port(value)->input && port_flush(s, value) == OT_UNWIND) return OT_UNWIND;
  as_port(value)->closed = true;
  otv cursor = s->open_ports, previous = ot_nil;
  while (is_type(cursor, OBJ_PAIR)) {
    if (as_pair(cursor)->car == value) {
      if (previous == ot_nil) s->open_ports = as_pair(cursor)->cdr;
      else ot_store(s, as_pair(previous), &as_pair(previous)->cdr, as_pair(cursor)->cdr);
      break;
    }
    previous = cursor;
    cursor = as_pair(cursor)->cdr;
  }
  if (as_port(value)->kind == PORT_CUSTOM) {
    otv callback = port_callback(s, value, "close");
    if (!is_falsy(callback) && apply_value(s, callback, NULL, 0, false) == OT_UNWIND)
      return OT_UNWIND;
  } else if (as_port(value)->kind == PORT_FILE) {
    int reason =
        s->io_host.close ? s->io_host.close(s->io_userdata, as_port(value)->handle) : OT_IO_OTHER;
    if (reason) return port_error(s, value, "close-port", reason);
  }
  return ot_nil;
}

#define PORT_CLOSE(NAME, DIR)                                                                      \
  static otv NAME(ots* s, otv* a, int n) {                                                         \
    if (need_arity(s, #NAME, n, 1, 1) == OT_UNWIND) return OT_UNWIND;                              \
    return port_close(s, a[0], DIR);                                                               \
  }
PORT_CLOSE(n_close_port, 0)
PORT_CLOSE(n_close_input, 1)
PORT_CLOSE(n_close_output, 2)
#undef PORT_CLOSE

static otv port_open_file(ots* s, otv* a, int n, bool input) {
  if (need_arity(s, "open-file", n, 1, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(a[0], OBJ_STRING) || (n == 2 && !is_type(a[1], OBJ_TABLE)))
    return ot_raise(s, "open-file: expected path and optional options table");
  otv path = a[0], opts = n == 2 ? a[1] : ot_nil, buffer = ot_nil;
  OT_FRAME_SCOPED(s, &path, &opts, &buffer);
  otv key = ot_intern(s, "buffer-size", 11, true);
  otv size = ot_table_get(s, opts, key, ot_make_int(4096));
  if (!ot_is_int(size) || ot_get_int(size) <= 0)
    return ot_raise(s, "open-file: invalid buffer-size");
  size_t capacity = (size_t)ot_get_int(size);
  if (capacity < 4) capacity = 4;
  unsigned flags = input ? OT_IO_INPUT : OT_IO_OUTPUT;
  if (!input) {
    key = ot_intern(s, "if-exists", 9, true);
    otv mode = ot_table_get(s, opts, key, ot_nil);
    if (mode != ot_nil) {
      if ((is_type(mode, OBJ_KEYWORD) && as_name(mode)->length == 6 &&
           memcmp(as_name(mode)->bytes, "append", 6) == 0))
        flags |= OT_IO_APPEND;
      else if ((is_type(mode, OBJ_KEYWORD) && as_name(mode)->length == 5 &&
                memcmp(as_name(mode)->bytes, "error", 5) == 0))
        flags |= OT_IO_EXCLUSIVE;
      else if (!(is_type(mode, OBJ_KEYWORD) && as_name(mode)->length == 8 &&
                 memcmp(as_name(mode)->bytes, "truncate", 8) == 0))
        return ot_raise(s, "open-output-file: invalid :if-exists");
    }
  }
  buffer = make_bytevector(s, capacity);
  const char* bytes;
  size_t length;
  ot_string_bytes(path, &bytes, &length);
  if (memchr(bytes, 0, length)) return ot_raise(s, "open-file: NUL in path");
  char* filename = ot_host_alloc(length + 1);
  if (!filename) return ot_raise(s, "open-file: out of memory");
  memcpy(filename, bytes, length);
  intptr_t handle = 0;
  int reason =
      s->io_host.open ? s->io_host.open(s->io_userdata, filename, flags, &handle) : OT_IO_NOT_FOUND;
  ot_host_free(filename);
  if (reason) return port_error(s, path, input ? "open-input-file" : "open-output-file", reason);
  return port_new(s, path, input, PORT_FILE, handle, buffer, ot_nil);
}

static otv n_open_input(ots* s, otv* a, int n) { return port_open_file(s, a, n, true); }

static otv n_open_output(ots* s, otv* a, int n) { return port_open_file(s, a, n, false); }

static otv port_memory(ots* s, otv* a, int n, unsigned kind) {
  bool input = kind == PORT_INPUT_MEMORY;
  if (need_arity(s, "open-memory-port", n, input ? 1 : 0, input ? 1 : 0) == OT_UNWIND)
    return OT_UNWIND;
  otv buffer = input ? a[0] : ot_nil, name = ot_nil;
  OT_FRAME_SCOPED(s, &buffer, &name);
  if (input) {
    if (is_type(buffer, OBJ_STRING)) buffer = nat_string_utf8(s, &buffer, 1);
    else if (is_type(buffer, OBJ_BYTEVECTOR)) buffer = nat_bytevector_copy(s, &buffer, 1);
    else return ot_raise(s, "open-input-port: expected string or bytevector");
  } else buffer = make_bytevector(s, 256);
  if (buffer == OT_UNWIND) return buffer;
  const char* label = kind == PORT_STRING ? "string" : "bytevector";
  name = ot_make_string(s, label, strlen(label));
  return port_new(s, name, input, kind, 0, buffer, ot_nil);
}

static otv n_input_string(ots* s, otv* a, int n) {
  if (n != 1 || !is_type(a[0], OBJ_STRING))
    return ot_raise(s, "open-input-string: expected string");
  return port_memory(s, a, n, PORT_INPUT_MEMORY);
}

static otv n_input_bytes(ots* s, otv* a, int n) {
  if (n != 1 || !is_type(a[0], OBJ_BYTEVECTOR))
    return ot_raise(s, "open-input-bytevector: expected bytevector");
  return port_memory(s, a, n, PORT_INPUT_MEMORY);
}

static otv n_output_string(ots* s, otv* a, int n) { return port_memory(s, a, n, PORT_STRING); }

static otv n_output_bytes(ots* s, otv* a, int n) { return port_memory(s, a, n, PORT_BYTES); }

static otv port_extract(ots* s, otv* a, int n, unsigned kind) {
  if (need_arity(s, "get-output", n, 1, 1) == OT_UNWIND ||
      port_check(s, a[0], false, "get-output") == OT_UNWIND)
    return OT_UNWIND;
  if (as_port(a[0])->kind != kind) return ot_raise(s, "get-output: wrong port kind");
  otv args[] = {as_port(a[0])->buffer, ot_make_int(0), ot_make_int((intptr_t)as_port(a[0])->end)};
  return kind == PORT_STRING ? nat_utf8_string(s, args, 3) : nat_bytevector_copy(s, args, 3);
}

static otv n_get_string(ots* s, otv* a, int n) { return port_extract(s, a, n, PORT_STRING); }

static otv n_get_bytes(ots* s, otv* a, int n) { return port_extract(s, a, n, PORT_BYTES); }

static otv n_write_bytes(ots* s, otv* a, int n) {
  if (need_arity(s, "write-bytevector", n, 1, 4) == OT_UNWIND ||
      bytevector_argument(s, "write-bytevector", a[0]) == OT_UNWIND)
    return OT_UNWIND;
  size_t start, end;
  if (span_arguments(s, "write-bytevector", a, n, 2, as_bytevector(a[0])->length, &start, &end) ==
      OT_UNWIND)
    return OT_UNWIND;
  return port_write_bytes(s, port_optional(s, a, n, 1, false), a[0], start, end);
}

static otv n_write_string(ots* s, otv* a, int n) {
  if (need_arity(s, "write-string", n, 1, 4) == OT_UNWIND) return OT_UNWIND;
  otv p = port_optional(s, a, n, 1, false), bytes = ot_nil;
  OT_FRAME_SCOPED(s, &p, &bytes);
  otv args[] = {a[0], n > 2 ? a[2] : ot_nil, n > 3 ? a[3] : ot_nil};
  bytes = nat_string_utf8(s, args, n > 1 ? n - 1 : 1);
  if (bytes == OT_UNWIND) return bytes;
  return port_write_bytes(s, p, bytes, 0, as_bytevector(bytes)->length);
}

static otv n_write_char(ots* s, otv* a, int n) {
  if (need_arity(s, "write-char", n, 1, 2) == OT_UNWIND) return OT_UNWIND;
  if (!ot_is_char(a[0])) return ot_raise(s, "write-char: expected character");
  otv p = port_optional(s, a, n, 1, false);
  OT_FRAME_SCOPED(s, &p);
  otv text = nat_string(s, a, 1);
  if (text == OT_UNWIND) return text;
  otv args[] = {text, p};
  return n_write_string(s, args, 2);
}

static otv n_write_u8(ots* s, otv* a, int n) {
  if (need_arity(s, "write-u8", n, 1, 2) == OT_UNWIND) return OT_UNWIND;
  if (!byte_value(a[0])) return ot_raise(s, "write-u8: expected byte");
  otv p = port_optional(s, a, n, 1, false);
  OT_FRAME_SCOPED(s, &p);
  unsigned char byte = (unsigned char)ot_get_int(a[0]);
  otv bytes = make_bytevector(s, 1);
  as_bytevector(bytes)->data[0] = byte;
  return port_write_bytes(s, p, bytes, 0, 1);
}

static otv port_read_into(ots* s, otv bytes, otv value, size_t start, size_t end) {
  if (port_check(s, value, true, "read-bytevector!") == OT_UNWIND) return OT_UNWIND;
  OT_FRAME_SCOPED(s, &value, &bytes);
  size_t at = start;
  while (at < end) {
    if (port_ensure(s, value, 1) == OT_UNWIND) return OT_UNWIND;
    ot_port_obj* p = as_port(value);
    size_t count = p->end - p->at;
    if (!count) return at == start ? ot_eof : ot_make_int((intptr_t)(at - start));
    if (count > end - at) count = end - at;
    memcpy(as_bytevector(bytes)->data + at, as_bytevector(p->buffer)->data + p->at, count);
    p->at += count;
    p->position += count;
    at += count;
  }
  return ot_make_int((intptr_t)(at - start));
}

static otv n_read_into(ots* s, otv* a, int n) {
  if (need_arity(s, "read-bytevector!", n, 1, 4) == OT_UNWIND ||
      bytevector_argument(s, "read-bytevector!", a[0]) == OT_UNWIND)
    return OT_UNWIND;
  size_t start, end;
  if (span_arguments(s, "read-bytevector!", a, n, 2, as_bytevector(a[0])->length, &start, &end) ==
      OT_UNWIND)
    return OT_UNWIND;
  return port_read_into(s, a[0], port_optional(s, a, n, 1, true), start, end);
}

static otv n_read_bytes(ots* s, otv* a, int n) {
  if (need_arity(s, "read-bytevector", n, 1, 2) == OT_UNWIND) return OT_UNWIND;
  if (!ot_is_int(a[0]) || ot_get_int(a[0]) < 0)
    return ot_raise(s, "read-bytevector: invalid count");
  size_t size = (size_t)ot_get_int(a[0]);
  otv p = port_optional(s, a, n, 1, true), bytes = ot_nil;
  OT_FRAME_SCOPED(s, &p, &bytes);
  bytes = make_bytevector(s, size);
  otv got = port_read_into(s, bytes, p, 0, size);
  if (got == OT_UNWIND || got == ot_eof) return got;
  if ((size_t)ot_get_int(got) == size) return bytes;
  otv args[] = {bytes, ot_make_int(0), got};
  return nat_bytevector_copy(s, args, 3);
}

static otv port_scan(ots* s, otv p, otv stops, bool until, bool skip, size_t limit,
                     bool eof_result) {
  OT_FRAME_SCOPED(s, &p, &stops);
  if (port_check(s, p, true, "read-string") == OT_UNWIND) return OT_UNWIND;
  bool ascii[128] = {false};
  if (stops != ot_nil) {
    const unsigned char* set = as_bytes(as_string(stops)->bytes)->data;
    for (size_t i = 0; i < as_string(stops)->length; i++)
      if (set[i] < 128) ascii[set[i]] = true;
  }
  buf out = {0};
  size_t count = 0;
  otv result = ot_nil;
  while (count < limit) {
    otv c = port_peek(s, p, true, false);
    if (c == OT_UNWIND) {
      result = c;
      break;
    }
    if (c == ot_eof) {
      if (!count && eof_result) result = c;
      break;
    }
    bool member = false;
    if (stops != ot_nil) {
      const unsigned char* set = as_bytes(as_string(stops)->bytes)->data;
      size_t length = as_string(stops)->length;
      if (ot_char_value(c) < 128) member = ascii[ot_char_value(c)];
      else
        for (size_t i = 0; i < length;) {
          if (utf8_decode(set, length, i) == ot_char_value(c)) {
            member = true;
            break;
          }
          i++;
          while (i < length && (set[i] & 0xc0) == 0x80) i++;
        }
      if (member == until) break;
    }
    ot_port_obj* port = as_port(p);
    size_t width =
        utf8_valid_sequence(as_bytevector(port->buffer)->data + port->at, port->end - port->at);
    if (!skip) buf_write(&out, (char*)as_bytevector(port->buffer)->data + port->at, width);
    port->at += width;
    port->position += width;
    count++;
  }
  if (result == ot_nil)
    result = skip         ? ot_make_int((intptr_t)count)
             : out.failed ? ot_raise(s, "read: out of memory")
                          : ot_make_string(s, out.data, out.length);
  buf_free(&out);
  return result;
}

static otv n_read_characters(ots* s, otv* a, int n) {
  if (need_arity(s, "read-string", n, 1, 2) == OT_UNWIND) return OT_UNWIND;
  if (!ot_is_int(a[0]) || ot_get_int(a[0]) < 0) return ot_raise(s, "read-string: invalid count");
  return port_scan(s, port_optional(s, a, n, 1, true), ot_nil, false, false,
                   (size_t)ot_get_int(a[0]), true);
}

static otv n_read_all(ots* s, otv* a, int n) {
  if (need_arity(s, "read-all", n, 0, 1) == OT_UNWIND) return OT_UNWIND;
  return port_scan(s, port_optional(s, a, n, 0, true), ot_nil, false, false, SIZE_MAX, false);
}

static otv port_scan_args(ots* s, otv* a, int n, bool until, bool skip) {
  if (need_arity(s, "scan", n, 1, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(a[0], OBJ_STRING)) return ot_raise(s, "scan: expected character set string");
  return port_scan(s, port_optional(s, a, n, 1, true), a[0], until, skip, SIZE_MAX, false);
}

static otv n_read_until(ots* s, otv* a, int n) { return port_scan_args(s, a, n, true, false); }

static otv n_read_while(ots* s, otv* a, int n) { return port_scan_args(s, a, n, false, false); }

static otv n_skip_while(ots* s, otv* a, int n) { return port_scan_args(s, a, n, false, true); }

static otv n_read_line(ots* s, otv* a, int n) {
  if (need_arity(s, "read-line", n, 0, 1) == OT_UNWIND) return OT_UNWIND;
  otv p = port_optional(s, a, n, 0, true), stops = ot_nil, text = ot_nil;
  OT_FRAME_SCOPED(s, &p, &stops, &text);
  otv first = port_peek(s, p, true, false);
  if (first == ot_eof || first == OT_UNWIND) return first;
  stops = ot_make_string(s, "\n", 1);
  text = port_scan(s, p, stops, true, false, SIZE_MAX, false);
  if (text == OT_UNWIND) return text;
  otv next = port_peek(s, p, true, true);
  if (next == OT_UNWIND) return next;
  size_t length = as_string(text)->length;
  if (next != ot_eof && length && as_bytes(as_string(text)->bytes)->data[length - 1] == '\r') {
    buf copy = {0};
    buf_write(&copy, (char*)as_bytes(as_string(text)->bytes)->data, length - 1);
    text = ot_make_string(s, copy.data, copy.length);
    buf_free(&copy);
  }
  return text;
}

static otv port_custom(ots* s, otv* a, int n, bool input) {
  if (need_arity(s, "make-port", n, 2, 2) == OT_UNWIND) return OT_UNWIND;
  if (!is_type(a[0], OBJ_STRING) || !is_type(a[1], OBJ_TABLE))
    return ot_raise(s, "make-port: expected name and callbacks");
  otv name = a[0], callbacks = a[1], buffer = ot_nil;
  OT_FRAME_SCOPED(s, &name, &callbacks, &buffer);
  const char* keys[] = {input ? "read" : "write", "close", "flush"};
  for (size_t i = 0; i < 3; i++) {
    otv key = ot_intern(s, keys[i], strlen(keys[i]), true);
    otv f = ot_table_get(s, callbacks, key, ot_nil);
    if ((i == 0 || !is_falsy(f)) && ot_value_type(f) != OT_TYPE_FUNCTION)
      return ot_raise(s, "make-port: expected callback procedure");
  }
  buffer = make_bytevector(s, 4096);
  return port_new(s, name, input, PORT_CUSTOM, 0, buffer, callbacks);
}

static otv n_make_input(ots* s, otv* a, int n) { return port_custom(s, a, n, true); }

static otv n_make_output(ots* s, otv* a, int n) { return port_custom(s, a, n, false); }

static otv n_open_ports(ots* s, otv* a, int n) {
  (void)a;
  if (need_arity(s, "open-ports", n, 0, 0) == OT_UNWIND) return OT_UNWIND;
  otv result = ot_array_new(s, 4), cursor = s->open_ports;
  OT_FRAME_SCOPED(s, &result, &cursor);
  while (is_type(cursor, OBJ_PAIR)) {
    array_push(s, &result, as_pair(cursor)->car);
    cursor = as_pair(cursor)->cdr;
  }
  for (size_t i = 0; i < 3; i++) {
    otv param = i == 0 ? s->input_port_param : i == 1 ? s->output_port_param : s->error_port_param;
    otv port = ((ot_param_obj*)ot_as_obj(param))->value;
    if (!as_port(port)->closed) array_push(s, &result, port);
  }
  return result;
}

static otv n_flush_all(ots* s, otv* a, int n) {
  (void)a;
  if (need_arity(s, "flush-ports!", n, 0, 0) == OT_UNWIND) return OT_UNWIND;
  otv cursor = s->open_ports, p = ot_nil;
  OT_FRAME_SCOPED(s, &cursor, &p);
  while (is_type(cursor, OBJ_PAIR)) {
    p = as_pair(cursor)->car;
    cursor = as_pair(cursor)->cdr;
    if (!as_port(p)->input && !as_port(p)->closed && port_flush(s, p) == OT_UNWIND)
      return OT_UNWIND;
  }
  p = ((ot_param_obj*)ot_as_obj(s->output_port_param))->value;
  if (!as_port(p)->closed && port_flush(s, p) == OT_UNWIND) return OT_UNWIND;
  p = ((ot_param_obj*)ot_as_obj(s->error_port_param))->value;
  if (!as_port(p)->closed && port_flush(s, p) == OT_UNWIND) return OT_UNWIND;
  return ot_nil;
}

bool ot_flush_ports(ots* s) {
  if (!is_type(s->output_port_param, OBJ_PARAM)) return true;
  otv condition = s->vm.condition, args = s->vm.unwind_args;
  OT_FRAME_SCOPED(s, &condition, &args);
  ot_unwind_kind kind = s->vm.unwind_kind;
  uint64_t restart = s->vm.unwind_restart_id;
  bool quit = s->quit_requested;
  int status = s->quit_status;
  s->vm.condition = s->vm.unwind_args = ot_nil;
  s->vm.unwind_kind = UNWIND_NONE;
  s->vm.unwind_restart_id = 0;
  s->quit_requested = false;
  if (n_flush_all(s, NULL, 0) == OT_UNWIND) return false;
  s->vm.condition = condition;
  s->vm.unwind_args = args;
  s->vm.unwind_kind = kind;
  s->vm.unwind_restart_id = restart;
  s->quit_requested = quit;
  s->quit_status = status;
  return true;
}

static char* port_path(ots* s, otv path, const char* op) {
  if (!is_type(path, OBJ_STRING)) {
    ot_raise(s, "%s: expected path string", op);
    return NULL;
  }
  const char* text;
  size_t size;
  ot_string_bytes(path, &text, &size);
  if (memchr(text, 0, size)) {
    ot_raise(s, "%s: NUL in path", op);
    return NULL;
  }
  char* copy = ot_host_alloc(size + 1);
  if (!copy) {
    ot_raise(s, "%s: out of memory", op);
    return NULL;
  }
  memcpy(copy, text, size);
  return copy;
}

static otv port_file_info(ots* s, otv* a, int n, bool exists) {
  const char* op = exists ? "file-exists?" : "file-info";
  if (need_arity(s, op, n, 1, 1) == OT_UNWIND) return OT_UNWIND;
  char* path = port_path(s, a[0], op);
  if (!path) return OT_UNWIND;
  ot_io_file_info info = {0};
  int reason = s->io_host.info ? s->io_host.info(s->io_userdata, path, &info) : OT_IO_OTHER;
  ot_host_free(path);
  if (exists && (reason == OT_IO_NOT_FOUND || reason == OT_IO_NOT_DIRECTORY)) return ot_false;
  if (reason) return port_error(s, a[0], op, reason);
  if (exists) return ot_true;
  otv result = ot_table_new(s, 3), key = ot_nil, value = ot_nil;
  OT_FRAME_SCOPED(s, &result, &key, &value);
  key = ot_intern(s, "type", 4, true);
  const char* kind = info.kind == 1 ? "file" : info.kind == 2 ? "directory" : "other";
  value = ot_intern(s, kind, strlen(kind), true);
  ot_table_put(s, result, key, value);
  key = ot_intern(s, "size", 4, true);
  value = info.size <= (uint64_t)OT_INT_MAX ? ot_make_int((intptr_t)info.size)
                                            : ot_make_float(s, (double)info.size);
  ot_table_put(s, result, key, value);
  key = ot_intern(s, "modified", 8, true);
  value = ot_make_float(s, info.modified);
  ot_table_put(s, result, key, value);
  return result;
}

static otv n_file_info(ots* s, otv* a, int n) { return port_file_info(s, a, n, false); }

static otv n_file_exists(ots* s, otv* a, int n) { return port_file_info(s, a, n, true); }

static otv port_file_change(ots* s, otv* a, int n, int operation) {
  const char* op = operation == 0   ? "delete-file"
                   : operation == 1 ? "make-directory"
                                    : "rename-file";
  int argc = operation == 2 ? 2 : 1;
  if (need_arity(s, op, n, argc, argc) == OT_UNWIND) return OT_UNWIND;
  char* path = port_path(s, a[0], op);
  if (!path) return OT_UNWIND;
  int reason = OT_IO_OTHER;
  if (operation == 0 && s->io_host.remove) reason = s->io_host.remove(s->io_userdata, path);
  if (operation == 1 && s->io_host.mkdir) reason = s->io_host.mkdir(s->io_userdata, path);
  if (operation == 2) {
    char* to = port_path(s, a[1], op);
    if (!to) {
      ot_host_free(path);
      return OT_UNWIND;
    }
    if (s->io_host.rename) reason = s->io_host.rename(s->io_userdata, path, to);
    ot_host_free(to);
  }
  ot_host_free(path);
  return reason ? port_error(s, a[0], op, reason) : ot_nil;
}

static otv n_delete_file(ots* s, otv* a, int n) { return port_file_change(s, a, n, 0); }

static otv n_mkdir(ots* s, otv* a, int n) { return port_file_change(s, a, n, 1); }

static otv n_rename_file(ots* s, otv* a, int n) { return port_file_change(s, a, n, 2); }

typedef struct port_directory {
  ots* state;
  otv result;
} port_directory;

static void port_directory_entry(void* user, const char* name) {
  port_directory* d = user;
  otv text = ot_make_string(d->state, name, strlen(name));
  array_push(d->state, &d->result, text);
}

static otv n_list_directory(ots* s, otv* a, int n) {
  if (need_arity(s, "list-directory", n, 1, 1) == OT_UNWIND) return OT_UNWIND;
  char* path = port_path(s, a[0], "list-directory");
  if (!path) return OT_UNWIND;
  port_directory d = {.state = s, .result = ot_array_new(s, 8)};
  OT_FRAME_SCOPED(s, &d.result);
  int reason = s->io_host.list ? s->io_host.list(s->io_userdata, path, port_directory_entry, &d)
                               : OT_IO_OTHER;
  ot_host_free(path);
  if (reason) return port_error(s, a[0], "list-directory", reason);
  /* The shared sort preserves array kind and Unicode string ordering. */
  otv symbol = ot_intern(s, "sort", 4, false);
  otv variable = namespace_var(s, s->core_namespace, symbol, false);
  return apply_value(s, ((ot_var_obj*)ot_as_obj(variable))->value, &d.result, 1, false);
}

static otv n_file_symbolic_link(ots* s, otv* a, int n) {
  const char* op = "file-symbolic-link?";
  if (need_arity(s, op, n, 1, 1) == OT_UNWIND) return OT_UNWIND;
  char* path = port_path(s, a[0], op);
  if (!path) return OT_UNWIND;
  bool result = false;
  int reason = s->io_host.symbolic_link
                 ? s->io_host.symbolic_link(s->io_userdata, path, &result) : OT_IO_OTHER;
  ot_host_free(path);
  if (reason == OT_IO_NOT_FOUND || reason == OT_IO_NOT_DIRECTORY) return ot_false;
  if (reason) return port_error(s, a[0], op, reason);
  return result ? ot_true : ot_false;
}

static otv n_current_directory(ots* s, otv* a, int n) {
  (void)a;
  if (need_arity(s, "current-directory", n, 0, 0) == OT_UNWIND) return OT_UNWIND;
  char* path = NULL;
  int reason = s->io_host.cwd ? s->io_host.cwd(s->io_userdata, &path) : OT_IO_OTHER;
  if (reason) return port_error(s, ot_nil, "current-directory", reason);
  otv result = ot_make_string(s, path, strlen(path));
  ot_host_free(path);
  return result;
}

static const nat_def port_natives[] = {{"flush-ports!", n_flush_all},
                                       {"file-exists?", n_file_exists},
                                       {"file-symbolic-link?", n_file_symbolic_link},
                                       {"current-directory", n_current_directory},
                                       {"file-info", n_file_info},
                                       {"delete-file", n_delete_file},
                                       {"rename-file", n_rename_file},
                                       {"make-directory", n_mkdir},
                                       {"list-directory", n_list_directory},
                                       {"display-to", n_display_to},
                                       {"write-to", n_write_to},
                                       {"port?", n_port_p},
                                       {"input-port?", n_input_p},
                                       {"output-port?", n_output_p},
                                       {"input-port-open?", n_input_open_p},
                                       {"output-port-open?", n_output_open_p},
                                       {"open-input-file", n_open_input},
                                       {"open-output-file", n_open_output},
                                       {"open-binary-input-file", n_open_input},
                                       {"open-binary-output-file", n_open_output},
                                       {"open-input-string", n_input_string},
                                       {"open-input-bytevector", n_input_bytes},
                                       {"open-output-string", n_output_string},
                                       {"open-output-bytevector", n_output_bytes},
                                       {"get-output-string", n_get_string},
                                       {"get-output-bytevector", n_get_bytes},
                                       {"close-port", n_close_port},
                                       {"close-input-port", n_close_input},
                                       {"close-output-port", n_close_output},
                                       {"read-char", n_read_char},
                                       {"peek-char", n_peek_char},
                                       {"read-u8", n_read_u8},
                                       {"peek-u8", n_peek_u8},
                                       {"read-bytevector", n_read_bytes},
                                       {"read-bytevector!", n_read_into},
                                       {"%read-characters", n_read_characters},
                                       {"read-all", n_read_all},
                                       {"read-line", n_read_line},
                                       {"read-until", n_read_until},
                                       {"read-while", n_read_while},
                                       {"skip-while", n_skip_while},
                                       {"write-string", n_write_string},
                                       {"write-char", n_write_char},
                                       {"write-u8", n_write_u8},
                                       {"write-bytevector", n_write_bytes},
                                       {"flush-output-port", n_flush},
                                       {"make-input-port", n_make_input},
                                       {"make-output-port", n_make_output},
                                       {"open-ports", n_open_ports}};

static otv port_param(ots* s, const char* name, const char* label, bool input, unsigned kind,
                      intptr_t handle) {
  otv symbol = ot_intern(s, name, strlen(name), false), value = ot_nil, buffer = ot_nil;
  OT_FRAME_SCOPED(s, &symbol, &value, &buffer);
  buffer = make_bytevector(s, 4096);
  value = ot_make_string(s, label, strlen(label));
  value = port_new(s, value, input, kind, handle, buffer, ot_nil);
  ot_param_obj* param = must_alloc(s, sizeof(*param), OBJ_PARAM);
  ot_store(s, param, &param->name, symbol);
  ot_store(s, param, &param->value, value);
  otv result = ot_from_obj(param);
  OT_FRAME_SCOPED(s, &result);
  define_var(s, s->core_namespace, symbol, result, ot_nil, false);
  return result;
}

static void ports_init(ots* s) {
  ot_set_io_host(s, NULL, NULL);
  s->open_ports = ot_null;
  s->input_port_param = port_param(s, "current-input-port", "stdin", true, PORT_STANDARD, 0);
  s->output_port_param = port_param(s, "current-output-port", "stdout", false, PORT_WRITER, 1);
  s->error_port_param = port_param(s, "current-error-port", "stderr", false, PORT_STANDARD, 2);
  otv old = s->vm.current_namespace;
  OT_FRAME_SCOPED(s, &old);
  s->vm.current_namespace = s->core_namespace;
  for (size_t i = 0; i < sizeof port_natives / sizeof *port_natives; i++)
    ot_def_nat(s, port_natives[i].name, port_natives[i].function);
  s->vm.current_namespace = old;
}
