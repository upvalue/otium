#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  size_t position, length, capacity, offset;
  unsigned kind;
  bool input, closed, eof, busy;
  ot_io_host host;
  void* userdata;
  intptr_t handle;
} oti_port;

static oti_port* info(ot_value p) { return object_bytes(p); }

void oti_port_repr(FILE* out, ot_value p) {
  if (info(p)->kind == 3 || info(p)->kind == 4) {
    fputs("#<port ", out);
    ot_write(NULL, out, object_fields(p)[4]);
    fprintf(out, " %s>", info(p)->input ? "input" : "output");
    return;
  }
  const char* name = info(p)->kind == 0     ? "\"string\""
                     : info(p)->kind == 1   ? "\"bytevector\""
                     : info(p)->handle == 0 ? "stdin"
                     : info(p)->handle == 1 ? "stdout"
                                            : "stderr";
  fprintf(out, "#<port %s %s>", name, info(p)->input ? "input" : "output");
}

static int posix_open(void* userdata, const char* path, unsigned flags, intptr_t* handle) {
  (void)userdata;
  int mode = flags & OT_IO_WRITE ? O_WRONLY : O_RDONLY;
  if (flags & OT_IO_CREATE) mode |= O_CREAT;
  if (flags & OT_IO_TRUNCATE) mode |= O_TRUNC;
  if (flags & OT_IO_APPEND) mode |= O_APPEND;
  if (flags & OT_IO_EXCLUSIVE) mode |= O_EXCL;
  int fd = open(path, mode, 0666);
  if (fd < 0) return errno;
  struct stat st;
  int error = fstat(fd, &st) ? errno : S_ISDIR(st.st_mode) ? EISDIR : 0;
  if (error) {
    close(fd);
    return error;
  }
  *handle = fd;
  return 0;
}

static long posix_read(void* userdata, intptr_t handle, uint8_t* bytes, size_t length) {
  (void)userdata;
  ssize_t n = read((int)handle, bytes, length);
  return n < 0 ? -errno : (long)n;
}

static long posix_write(void* userdata, intptr_t handle, const uint8_t* bytes, size_t length) {
  (void)userdata;
  ssize_t n = write((int)handle, bytes, length);
  return n < 0 ? -errno : (long)n;
}

static int posix_close(void* userdata, intptr_t handle) {
  (void)userdata;
  return close((int)handle) ? errno : 0;
}

static const ot_io_host posix_host = {posix_open, posix_read, posix_write, posix_close, NULL};

bool ot_set_io_host(ot_runtime* rt, const ot_io_host* host, void* userdata) {
  if (!oti_host_entry(rt)) return false;
  if (host && (!host->open || !host->read || !host->write || !host->close))
    return oti_error(rt, "I/O host requires open, read, write and close callbacks");
  rt->io_host = host ? *host : posix_host;
  rt->io_host_set = host != NULL;
  rt->io_userdata = host ? userdata : NULL;
  return true;
}

static void port_host(ot_runtime* rt, ot_value port) {
  info(port)->host = rt->io_host_set ? rt->io_host : posix_host;
  info(port)->userdata = rt->io_host_set ? rt->io_userdata : NULL;
}

static void host_begin(ot_runtime* rt, ot_value port) {
  rt->host_io_active = true;
  info(port)->busy = true;
}

static bool host_end(ot_runtime* rt, ot_value port) {
  rt->host_io_active = false;
  info(port)->busy = false;
  if (!rt->host_io_reentry) return true;
  rt->host_io_reentry = false;
  return oti_error(rt, "host I/O callback cannot reenter Otium");
}

static ot_value fail(ot_runtime* rt, const char* message) {
  oti_error(rt, message);
  return 0;
}

static bool natural(ot_value v, size_t* n) {
  if (!ot_is_int(v) || ot_to_int(v) < 0) return false;
  *n = (size_t)ot_to_int(v);
  return true;
}

static bool register_port(ot_runtime* rt, ot_value p) {
  if (!oti_grow(rt, (void**)&rt->open_ports, &rt->open_port_capacity, rt->open_port_count + 1,
               sizeof(ot_value)))
    return false;
  rt->open_port_roots.slots = rt->open_ports;
  rt->open_ports[rt->open_port_count++] = p;
  rt->open_port_roots.count = rt->open_port_count;
  return true;
}

static void unregister_port(ot_runtime* rt, ot_value p) {
  for (size_t i = 0; i < rt->open_port_count; i++)
    if (rt->open_ports[i] == p) {
      memmove(rt->open_ports + i, rt->open_ports + i + 1,
              (rt->open_port_count - i - 1) * sizeof(ot_value));
      rt->open_port_roots.count = --rt->open_port_count;
      return;
    }
}

static bool callable(ot_value v) {
  return (v & 255) == TAG_BUILTIN || oti_kind(v, K_CLOSURE) || oti_kind(v, K_GENERIC) ||
         oti_kind(v, K_NEXT) || oti_kind(v, K_INSTANCE) || oti_kind(v, K_RECORD) ||
         oti_kind(v, K_MACRO) || oti_kind(v, K_PARAM) || oti_kind(v, K_FOREIGN_FUNCTION) ||
         oti_collection(v);
}

/* The caller roots *port. Never retain its opaque bytes across a callback. */
static ot_value callback(ot_runtime* rt, ot_value* port, size_t field, bool bytes, size_t start,
                      size_t end) {
  ot_value function = object_fields(*port)[field];
  if (function == OT_NIL) return OT_NIL;
  if (info(*port)->busy) return fail(rt, "port callback reentry");
  ot_value v[] = {function, OT_EMPTY, object_fields(*port)[0], ot_int((intptr_t)start),
               ot_int((intptr_t)end)};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, v, 5);
  bool ok = true;
  if (bytes)
    for (size_t i = 3; i; i--) {
      ot_value pair = oti_alloc(rt, K_PAIR, 2, 0);
      if (!pair) {
        ok = false;
        break;
      }
      object_fields(pair)[0] = v[i + 1];
      object_fields(pair)[1] = v[1];
      v[1] = pair;
    }
  ot_value result = 0;
  if (ok) {
    info(*port)->busy = true;
    result = oti_call_list(rt, v[0], v[1]);
    info(*port)->busy = false;
  }
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static void host_error(ot_runtime* rt, ot_value* port, const char* op, int error) {
  if (!ot_poll_interrupt(rt)) return;
  if (info(*port)->kind == 4) oti_file_error(rt, op, object_fields(*port)[4], error ? error : EIO);
  else oti_io_error(rt, ":other");
}

static long transfer(ot_runtime* rt, ot_value* port, bool writing, size_t start, size_t length) {
  for (;;) {
    if (!ot_poll_interrupt(rt)) return -1;
    oti_port* p = info(*port);
    unsigned char* bytes = oti_bytes_data(object_fields(*port)[0]) + start;
    host_begin(rt, *port);
    long n = writing ? p->host.write(p->userdata, p->handle, bytes, length)
                     : p->host.read(p->userdata, p->handle, bytes, length);
    if (!host_end(rt, *port)) return -1;
    if (n == -EINTR) {
      if (!ot_poll_interrupt(rt)) return -1;
      continue;
    }
    if (n < 0) {
      host_error(rt, port, writing ? "write" : "read", n < -INT_MAX ? EIO : (int)-n);
      return -1;
    }
    if ((size_t)n > length || (writing && !n)) {
      oti_error(rt, writing ? "invalid host write count" : "invalid host read count");
      return -1;
    }
    return n;
  }
}

static bool flush(ot_runtime* rt, ot_value* port) {
  if (info(*port)->kind < 2) return true;
  while (info(*port)->offset < info(*port)->length) {
    size_t left = info(*port)->length - info(*port)->offset;
    size_t written;
    if (info(*port)->kind == 3) {
      ot_value n = callback(rt, port, 1, true, info(*port)->offset, info(*port)->length);
      if (!n) return false;
      if (!natural(n, &written) || !written || written > left)
        return oti_error(rt, "invalid custom port write count");
    } else {
      long n = transfer(rt, port, true, info(*port)->offset, left);
      if (n < 0) return false;
      written = (size_t)n;
    }
    info(*port)->offset += written;
  }
  info(*port)->offset = info(*port)->length = 0;
  if (info(*port)->kind == 3) return callback(rt, port, 2, false, 0, 0) != 0;
  oti_port* p = info(*port);
  if (p->host.flush) {
    host_begin(rt, *port);
    int error = p->host.flush(p->userdata, p->handle);
    if (!host_end(rt, *port)) return false;
    if (error) {
      host_error(rt, port, "flush-output-port", error);
      return false;
    }
  }
  return true;
}

static ot_value make_custom(ot_runtime* rt, ot_value* port, size_t first, size_t count, bool input) {
  if (count != 2 || !oti_kind(rt->stack[first], K_STRING) || !oti_kind(rt->stack[first + 1], K_TABLE))
    return fail(rt, "custom port needs a name and callback table");
  ot_value callbacks[] = {OT_NIL, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, callbacks, 3);
  const char* names[] = {input ? ":read" : ":write", ":flush", ":close"};
  bool ok = true;
  for (size_t i = 0; i < 3; i++) {
    ot_value key = oti_intern(rt, K_KEYWORD, names[i], strlen(names[i]));
    if (!key) {
      ok = false;
      break;
    }
    callbacks[i] = oti_table_get(rt, rt->stack[first + 1], key, OT_NIL);
    if (!callbacks[i]) {
      ok = false;
      break;
    }
    if ((!i || callbacks[i] != OT_NIL) && !callable(callbacks[i])) {
      fail(rt, "invalid custom port callback");
      ok = false;
      break;
    }
  }
  if (ok) {
    *port = oti_alloc(rt, K_PORT, 5, sizeof(oti_port));
    ok = *port != 0;
    if (ok) {
      info(*port)->input = input;
      info(*port)->kind = 3;
      info(*port)->capacity = 4096;
      for (size_t i = 0; i < 3; i++) object_fields(*port)[i + 1] = callbacks[i];
      object_fields(*port)[4] = rt->stack[first];
      ot_value buffer = oti_bytes_new(rt, 4096);
      ok = buffer != 0;
      if (ok) {
        object_fields(*port)[0] = buffer;
        ok = register_port(rt, *port);
      }
    }
  }
  oti_roots_pop(&rt->heap, &roots);
  return ok ? *port : 0;
}

static ot_value open_file(ot_runtime* rt, ot_value* port, size_t first, size_t count, bool input) {
  if (count < 1 || count > 2 || !oti_kind(rt->stack[first], K_STRING) ||
      (count == 2 && !oti_kind(rt->stack[first + 1], K_TABLE)))
    return fail(rt, "file port needs a path and optional options table");
  size_t size = 4096;
  unsigned flags =
      input ? OT_IO_READ : OT_IO_WRITE | OT_IO_CREATE | OT_IO_TRUNCATE;
  if (count == 2) {
    ot_value key = oti_intern(rt, K_KEYWORD, ":buffer-size", 12);
    if (!key) return 0;
    ot_value option = oti_table_get(rt, rt->stack[first + 1], key, OT_NIL);
    if (!option) return 0;
    if (option != OT_NIL && (!natural(option, &size) || !size))
      return fail(rt, "invalid port buffer size");
    if (!input) {
      key = oti_intern(rt, K_KEYWORD, ":if-exists", 10);
      if (!key) return 0;
      option = oti_table_get(rt, rt->stack[first + 1], key, OT_NIL);
      if (!option) return 0;
      if (option != OT_NIL) {
        if (!oti_kind(option, K_KEYWORD)) return fail(rt, "invalid if-exists option");
        const char* name = (char*)oti_bytes_data(option);
        if (!strcmp(name, ":append")) {
          flags = OT_IO_WRITE | OT_IO_CREATE | OT_IO_APPEND;
        } else if (!strcmp(name, ":error"))
          flags = OT_IO_WRITE | OT_IO_CREATE | OT_IO_EXCLUSIVE;
        else if (strcmp(name, ":truncate")) return fail(rt, "invalid if-exists option");
      }
    }
  }
  if (size < 4) size = 4;
  size_t length = oti_bytes_length(rt->stack[first]);
  if (memchr(oti_bytes_data(rt->stack[first]), 0, length)) return fail(rt, "file path contains NUL");
  *port = oti_alloc(rt, K_PORT, 5, sizeof(oti_port));
  if (!*port) return 0;
  object_fields(*port)[4] = rt->stack[first];
  ot_value buffer = oti_bytes_new(rt, size);
  if (!buffer) return 0;
  object_fields(*port)[0] = buffer;
  info(*port)->kind = 4;
  info(*port)->input = input;
  info(*port)->capacity = size;
  port_host(rt, *port);
  oti_port* p = info(*port);
  host_begin(rt, *port);
  int error =
      p->host.open(p->userdata, (char*)oti_bytes_data(object_fields(*port)[4]), flags, &p->handle);
  bool entered = host_end(rt, *port);
  if (error) {
    if (!entered) return 0;
    return oti_file_error(rt, input ? "open-input-file" : "open-output-file",
                         object_fields(*port)[4], error);
  }
  if (!entered || !register_port(rt, *port)) {
    host_begin(rt, *port);
    p->host.close(p->userdata, p->handle);
    host_end(rt, *port);
    info(*port)->closed = true;
    return 0;
  }
  return *port;
}

/* Destruction invokes no Otium code. Native hosts may drain buffered output;
 * failures are ignored here, so embedders should call ot_flush_ports first. */
void oti_ports_destroy(ot_runtime* rt) {
  for (size_t i = 0; i < rt->open_port_count; i++) {
    ot_value port = rt->open_ports[i];
    oti_port* p = info(port);
    if ((p->kind != 2 && p->kind != 4) || p->closed) continue;
    host_begin(rt, port);
    if (!p->input) {
      while (p->offset < p->length) {
        size_t left = p->length - p->offset;
        long n = p->host.write(p->userdata, p->handle,
                               oti_bytes_data(object_fields(port)[0]) + p->offset, left);
        if (n <= 0 || (size_t)n > left) break;
        p->offset += (size_t)n;
      }
      if (p->host.flush) p->host.flush(p->userdata, p->handle);
    }
    if (p->kind == 4) p->host.close(p->userdata, p->handle);
    host_end(rt, port);
    p->closed = true;
  }
}

static ot_value standard(ot_runtime* rt, unsigned which) {
  if (rt->standard_ports[which] == OT_NIL) {
    ot_value p = oti_alloc(rt, K_PORT, 5, sizeof(oti_port));
    if (!p) return 0;
    oti_roots roots;
    oti_roots_push(&rt->heap, &roots, &p, 1);
    info(p)->kind = 2;
    info(p)->input = which == 0;
    info(p)->handle = (intptr_t)which;
    info(p)->capacity = 4096;
    port_host(rt, p);
    ot_value buffer = oti_bytes_new(rt, 4096);
    bool ok = buffer != 0;
    if (ok) {
      object_fields(p)[0] = buffer;
      ok = register_port(rt, p);
    }
    if (ok) rt->standard_ports[which] = p;
    oti_roots_pop(&rt->heap, &roots);
    if (!ok) return 0;
  }
  return rt->standard_ports[which];
}

static ot_value current_port(ot_runtime* rt, unsigned which) {
  const char* name = which == 0   ? "current-input-port"
                     : which == 1 ? "current-output-port"
                                  : "current-error-port";
  uint32_t index = oti_binding_global(rt, 0, name, strlen(name), false);
  if (index == UINT32_MAX || rt->globals[index] == V_UNBOUND) return standard(rt, which);
  ot_value p = rt->globals[index];
  if (!oti_kind(p, K_PARAM)) return fail(rt, "current port binding must be a param");
  return oti_param_call(rt, p, 0);
}

static ot_value current(ot_runtime* rt, bool input) { return current_port(rt, input ? 0 : 1); }

static bool check(ot_runtime* rt, ot_value p, bool input) {
  if (!oti_kind(p, K_PORT) || info(p)->input != input)
    return oti_error(rt, input ? "expected input port" : "expected output port");
  if (info(p)->busy) return oti_error(rt, "port callback reentry");
  if (info(p)->closed) {
    oti_io_error(rt, ":closed");
    return false;
  }
  return true;
}

static int peek(ot_runtime* rt, ot_value* port, size_t ahead) {
  oti_port* s = info(*port);
  if (s->kind < 2)
    return ahead < s->length - s->position
               ? oti_bytes_data(object_fields(*port)[0])[s->position + ahead]
               : -1;
  while (info(*port)->length - info(*port)->offset <= ahead) {
    s = info(*port);
    if (s->eof) return -1;
    size_t remaining = s->length - s->offset;
    unsigned char* data = oti_bytes_data(object_fields(*port)[0]);
    memmove(data, data + s->offset, remaining);
    s->offset = 0;
    s->length = remaining;
    size_t available = s->capacity - remaining, n;
    if (s->kind == 3) {
      ot_value result = callback(rt, port, 1, true, remaining, s->capacity);
      if (!result) return -2;
      if (result == OT_EOF) {
        info(*port)->eof = true;
        return -1;
      }
      if (!natural(result, &n) || !n || n > available) {
        fail(rt, "invalid custom port read count");
        return -2;
      }
    } else {
      long result = transfer(rt, port, false, remaining, available);
      if (result < 0) return -2;
      if (!result) {
        info(*port)->eof = true;
        return -1;
      }
      n = (size_t)result;
    }
    info(*port)->length += n;
  }
  return oti_bytes_data(object_fields(*port)[0])[info(*port)->offset + ahead];
}

static void consume(ot_value p, size_t n) {
  oti_port* s = info(p);
  s->position += n;
  if (s->kind >= 2) s->offset += n;
}

static int character(ot_runtime* rt, ot_value* port, uint32_t* cp, size_t* width) {
  int first = peek(rt, port, 0);
  if (first < 0) return first;
  unsigned char bytes[4];
  bytes[0] = (unsigned char)first;
  *width = first < 128                      ? 1
           : first >= 0xc2 && first <= 0xdf ? 2
           : first >= 0xe0 && first <= 0xef ? 3
           : first >= 0xf0 && first <= 0xf4 ? 4
                                            : 0;
  bool valid = *width != 0;
  for (size_t i = 1; valid && i < *width; i++) {
    int b = peek(rt, port, i);
    if (b == -2) return -2;
    if (b < 0) valid = false;
    else bytes[i] = (unsigned char)b;
  }
  if (!valid || !oti_utf8_scalar(bytes, 0, *width)) {
    oti_decode_error(rt, info((*port))->position);
    return -2;
  }
  oti_utf8_next((char*)bytes, *width, 0, cp);
  return 0;
}

static bool datum_space(uint32_t c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

static bool datum_delimiter(uint32_t c) {
  return datum_space(c) || (c < 128 && strchr("()[]{}\";'`,", (int)c));
}

/* Only establish the boundary here; the shared reader validates the syntax. */
static ot_value read_datum(ot_runtime* rt, ot_value* port) {
  enum { NORMAL, ATOM, STRING, ESCAPE, COMMENT, CHAR_FIRST, CHAR_TOKEN } mode = NORMAL;

  size_t depth = 0, length = 0, capacity = 0, token = 0;
  char closers[512];
  bool started = false, comma = false, done = false;
  char* source = NULL;
  while (!done) {
    if (!ot_poll_interrupt(rt)) {
      free(source);
      return 0;
    }
    uint32_t cp;
    size_t width;
    int state = character(rt, port, &cp, &width);
    if (state == -2) {
      free(source);
      return 0;
    }
    if (state == -1) break;
    bool delim = datum_delimiter(cp);
    if (mode == ATOM) {
      if (length - token == 1 && source[token] == '#' && cp == '\\') mode = CHAR_FIRST;
      else if (length - token == 1 && source[token] == '#' && cp == '`') mode = NORMAL;
      else if (length - token == 3 && !memcmp(source + token, "#u8", 3) && cp == '(') {
        if (depth < sizeof(closers)) closers[depth] = ')';
        depth++;
        mode = NORMAL;
      } else if (delim) {
        if (!depth) break;
        mode = NORMAL;
        continue;
      }
    } else if (mode == CHAR_FIRST) {
      mode = delim ? NORMAL : CHAR_TOKEN;
      if (delim && !depth) done = true;
    } else if (mode == CHAR_TOKEN) {
      if (delim) {
        if (!depth) break;
        mode = NORMAL;
        continue;
      }
    } else if (mode == COMMENT) {
      if (cp == '\n' || cp == '\r') mode = NORMAL;
    } else if (mode == ESCAPE) mode = STRING;
    else if (mode == STRING) {
      if (cp == '\\') mode = ESCAPE;
      else if (cp == '"') {
        mode = NORMAL;
        if (!depth) done = true;
      }
    } else {
      if (comma && cp == '@') comma = false;
      else {
        comma = false;
        if (cp == ';') mode = COMMENT;
        else if (!datum_space(cp)) {
          started = true;
          if (cp == '(' || cp == '[' || cp == '{') {
            if (depth < sizeof(closers)) closers[depth] = cp == '(' ? ')' : cp == '[' ? ']' : '}';
            depth++;
          } else if (cp == ')' || cp == ']' || cp == '}') {
            if (!depth || closers[depth - 1] != (char)cp) done = true;
            else if (!--depth) done = true;
          } else if (cp == '"') mode = STRING;
          else if (cp == ',' || cp == '\'' || cp == '`') comma = cp == ',';
          else {
            token = length;
            mode = ATOM;
          }
        }
      }
    }
    if (depth > 512 || length > 16 * 1024 * 1024 - width) {
      free(source);
      return fail(rt, "port datum exceeds reader limit");
    }
    if (!oti_grow(rt, (void**)&source, &capacity, length + width, 1)) {
      free(source);
      return 0;
    }
    char encoded[4];
    size_t used = oti_utf8_encode(cp, encoded);
    memcpy(source + length, encoded, used);
    length += used;
    consume((*port), width);
  }
  ot_value result = started ? oti_read_source(rt, source, length) : OT_EOF;
  free(source);
  return result;
}

static bool append(ot_runtime* rt, ot_value* port, const char* bytes, size_t n) {
  if (!check(rt, (*port), false)) return false;
  if (info(*port)->kind >= 2) {
    while (n) {
      if (info(*port)->length == info(*port)->capacity && !flush(rt, port)) return false;
      size_t room = info(*port)->capacity - info(*port)->length;
      size_t take = n < room ? n : room;
      memcpy(oti_bytes_data(object_fields(*port)[0]) + info(*port)->length, bytes, take);
      info(*port)->length += take;
      bytes += take;
      n -= take;
      if (info(*port)->length == info(*port)->capacity && !flush(rt, port)) return false;
    }
    return info(*port)->kind != 2 || flush(rt, port);
  }
  size_t length = info((*port))->length;
  if (n > SIZE_MAX - length) return oti_error(rt, "port output too large");
  if (length + n > info((*port))->capacity) {
    size_t capacity = info((*port))->capacity;
    if (capacity > SIZE_MAX / 2) capacity = length + n;
    else capacity *= 2;
    if (capacity < 64) capacity = 64;
    if (capacity < length + n) capacity = length + n;
    ot_value data = oti_alloc(rt, K_BUFFER_DATA, 0, capacity);
    if (data) {
      if (length) memcpy(object_bytes(data), object_bytes(object_fields((*port))[0]), length);
      object_fields((*port))[0] = data;
      info((*port))->capacity = capacity;
    }
    if (!data) return false;
  }
  if (n) memcpy((char*)object_bytes(object_fields((*port))[0]) + length, bytes, n);
  info((*port))->length += n;
  return true;
}

ot_value oti_port_print(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  ot_value p;
  if (id == B_PROCESS_WRITE_ERROR) {
    if (count != 1) return fail(rt, "write-error needs one argument");
    p = current_port(rt, 2);
  } else if (id == B_DISPLAY_TO || id == B_WRITE_TO) {
    if (!count) return fail(rt, "output needs a port");
    p = rt->stack[first++];
    count--;
  } else if (id == B_NEWLINE) {
    if (count > 1) return fail(rt, "newline takes an optional port");
    p = count ? rt->stack[first] : current(rt, false);
    count = 0;
  } else p = current(rt, false);
  if (!p || !check(rt, p, false)) return 0;
  char* bytes = NULL;
  size_t length = 0;
  FILE* stream = open_memstream(&bytes, &length);
  if (!stream) return fail(rt, "out of host memory");
  for (size_t i = 0; i < count; i++) {
    if (i) fputc(' ', stream);
    if (id == B_WRITE || id == B_WRITE_TO) ot_write(rt, stream, rt->stack[first + i]);
    else ot_print(rt, stream, rt->stack[first + i]);
  }
  if (id == B_PRINTLN || id == B_NEWLINE) fputc('\n', stream);
  bool ok = !ferror(stream);
  if (fclose(stream)) ok = false;
  oti_roots root;
  oti_roots_push(&rt->heap, &root, &p, 1);
  if (ok) ok = append(rt, &p, bytes, length);
  else oti_error(rt, "cannot format port output");
  oti_roots_pop(&rt->heap, &root);
  free(bytes);
  return ok ? OT_NIL : 0;
}

static bool range(ot_runtime* rt, size_t first, size_t count, size_t n, size_t* start, size_t* end) {
  *start = 0;
  *end = n;
  if ((count && !natural(rt->stack[first], start)) ||
      (count > 1 && !natural(rt->stack[first + 1], end)) || *start > *end || *end > n)
    return oti_error(rt, "invalid port slice");
  return true;
}

static ot_value port_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count, ot_value live[2]) {
  if (id >= B_OPEN_INPUT_FILE)
    return open_file(rt, &live[0], first, count,
                     id == B_OPEN_INPUT_FILE || id == B_OPEN_BINARY_INPUT);
  if (id == B_MAKE_INPUT_PORT || id == B_MAKE_OUTPUT_PORT)
    return make_custom(rt, &live[0], first, count, id == B_MAKE_INPUT_PORT);
  if (id == B_OPEN_PORTS) {
    if (count) return fail(rt, "open-ports takes no arguments");
    size_t base = rt->sp, n = rt->open_port_count;
    if (n > rt->stack_limit - base || !oti_stack(rt, base + n))
      return fail(rt, "VM stack limit exceeded");
    if (n) memcpy(rt->stack + base, rt->open_ports, n * sizeof(ot_value));
    rt->sp = rt->stack_roots.count = base + n;
    ot_value result = oti_collection_builtin(rt, B_ARRAY, base, n);
    rt->sp = rt->stack_roots.count = base;
    return result;
  }
  if (id == B_DISPLAY_TO || id == B_WRITE_TO) return oti_port_print(rt, id, first, count);
  if (id >= B_STDIN) {
    if (count) return fail(rt, "standard port takes no arguments");
    return standard(rt, id - B_STDIN);
  }
  if (id <= B_OUTPUT_OPEN) {
    if (count != 1) return fail(rt, "port predicate takes one argument");
    live[0] = rt->stack[first];
    bool yes = oti_kind(live[0], K_PORT);
    if (yes && id != B_PORT_P)
      yes = info(live[0])->input == (id == B_INPUT_P || id == B_INPUT_OPEN);
    if (yes && (id == B_INPUT_OPEN || id == B_OUTPUT_OPEN)) yes = !info(live[0])->closed;
    return yes ? OT_TRUE : OT_FALSE;
  }
  if (id == B_OPEN_INPUT_STRING || id == B_OPEN_INPUT_BYTES || id == B_OPEN_OUTPUT_STRING ||
      id == B_OPEN_OUTPUT_BYTES) {
    bool input = id == B_OPEN_INPUT_STRING || id == B_OPEN_INPUT_BYTES;
    bool string = id == B_OPEN_INPUT_STRING || id == B_OPEN_OUTPUT_STRING;
    if (count != (input ? 1u : 0u)) return fail(rt, "wrong number of port constructor arguments");
    size_t n = 0;
    if (input) {
      if (!oti_kind(rt->stack[first], string ? K_STRING : K_BYTEVECTOR))
        return fail(rt, "invalid input source");
      n = oti_bytes_length(rt->stack[first]);
    }
    live[0] = oti_alloc(rt, K_PORT, 5, sizeof(oti_port));
    if (!live[0]) return 0;
    info(live[0])->input = input;
    info(live[0])->kind = string ? 0 : 1;
    info(live[0])->length = n;
    if (input) {
      ot_value bytes = oti_bytes_new(rt, n);
      if (bytes) {
        if (n) memcpy(oti_bytes_data(bytes), oti_bytes_data(rt->stack[first]), n);
        object_fields(live[0])[0] = bytes;
      }
      if (!bytes) return 0;
    }
    return live[0];
  }
  if (id == B_CLOSE_PORT || id == B_CLOSE_INPUT || id == B_CLOSE_OUTPUT) {
    if (count != 1 || !oti_kind(rt->stack[first], K_PORT)) return fail(rt, "close needs a port");
    live[0] = rt->stack[first];
    if ((id == B_CLOSE_INPUT && !info(live[0])->input) ||
        (id == B_CLOSE_OUTPUT && info(live[0])->input))
      return fail(rt, "wrong port direction");
    if (info(live[0])->closed) return OT_NIL;
    if (info(live[0])->busy) return fail(rt, "port callback reentry");
    if (!info(live[0])->input && !flush(rt, &live[0])) return 0;
    if (info(live[0])->kind == 3 && !callback(rt, &live[0], 3, false, 0, 0)) return 0;
    int error = 0;
    if (info(live[0])->kind == 4) {
      oti_port* p = info(live[0]);
      host_begin(rt, live[0]);
      error = p->host.close(p->userdata, p->handle);
      bool entered = host_end(rt, live[0]);
      info(live[0])->closed = true;
      unregister_port(rt, live[0]);
      if (!entered) return 0;
      return error ? oti_file_error(rt, "close-port", object_fields(live[0])[4], error) : OT_NIL;
    }
    info(live[0])->closed = true;
    unregister_port(rt, live[0]);
    return error ? oti_file_error(rt, "close-port", object_fields(live[0])[4], error) : OT_NIL;
  }
  if (id == B_GET_OUTPUT_STRING || id == B_GET_OUTPUT_BYTES) {
    if (count != 1) return fail(rt, "output extraction needs a port");
    live[0] = rt->stack[first];
    if (!check(rt, live[0], false)) return 0;
    if (info(live[0])->kind != (id == B_GET_OUTPUT_STRING ? 0u : 1u))
      return fail(rt, "wrong output port kind");
    size_t n = info(live[0])->length;
    char* copy = malloc(n + 1);
    if (!copy) return fail(rt, "out of host memory");
    if (n) memcpy(copy, object_bytes(object_fields(live[0])[0]), n);
    ot_value result;
    if (id == B_GET_OUTPUT_STRING) {
      for (size_t i = 0; i < n;) {
        size_t next = oti_utf8_scalar((unsigned char*)copy, i, n);
        if (!next) {
          free(copy);
          return oti_decode_error(rt, i);
        }
        i = next;
      }
      result = oti_text(rt, K_STRING, copy, n);
    } else {
      result = oti_bytes_new(rt, n);
      if (result && n) memcpy(oti_bytes_data(result), copy, n);
    }
    free(copy);
    return result;
  }
  if (id == B_READ_DATUM) {
    if (count > 1) return fail(rt, "read takes an optional port");
    live[0] = count ? rt->stack[first] : current(rt, true);
    if (!live[0] || !check(rt, live[0], true)) return 0;
    return read_datum(rt, &live[0]);
  }
  bool output = id >= B_WRITE_U8 && id <= B_FLUSH_PORT;
  bool payload = (id >= B_READ_CHARACTERS && id <= B_READ_BYTES_MUT) ||
                 (id >= B_WRITE_U8 && id <= B_WRITE_BYTES) || id >= B_READ_UNTIL;
  size_t min = payload ? 1 : 0, max = min + 1;
  if (id == B_READ_BYTES_MUT || id == B_WRITE_STRING || id == B_WRITE_BYTES) max = 4;
  if (count < min || count > max) return fail(rt, "wrong number of port arguments");
  live[0] = count > min ? rt->stack[first + min] : current(rt, !output);
  if (!live[0] || !check(rt, live[0], !output)) return 0;
  live[1] = payload ? rt->stack[first] : OT_NIL;
  if (output) {
    if (id == B_FLUSH_PORT) {
      return flush(rt, &live[0]) ? OT_NIL : 0;
    }
    char one[4];
    const char* source = one;
    size_t n = 1;
    if (id == B_WRITE_U8) {
      size_t byte;
      if (!natural(live[1], &byte) || byte > 255) return fail(rt, "expected byte");
      one[0] = (char)byte;
    } else if (id == B_WRITE_CHAR) {
      if ((live[1] & 255) != TAG_CHAR) return fail(rt, "expected character");
      n = oti_utf8_encode((uint32_t)(live[1] >> 8), one);
    } else {
      bool string = id == B_WRITE_STRING;
      if (!oti_kind(live[1], string ? K_STRING : K_BYTEVECTOR))
        return fail(rt, "invalid output data");
      size_t length = oti_bytes_length(live[1]), items = length;
      source = (char*)oti_bytes_data(live[1]);
      if (string) {
        items = 0;
        for (size_t i = 0; i < length; items++) {
          uint32_t cp;
          i = oti_utf8_next(source, length, i, &cp);
        }
      }
      size_t start, end;
      if (!range(rt, first + 2, count > 2 ? count - 2 : 0, items, &start, &end)) return 0;
      if (string) {
        size_t lo = 0, hi;
        uint32_t cp;
        for (size_t i = 0; i < start; i++) lo = oti_utf8_next(source, length, lo, &cp);
        hi = lo;
        for (size_t i = start; i < end; i++) hi = oti_utf8_next(source, length, hi, &cp);
        start = lo;
        end = hi;
      }
      source += start;
      n = end - start;
    }
    char* copy = malloc(n + 1);
    if (!copy) return fail(rt, "out of host memory");
    memcpy(copy, source, n);
    bool ok = append(rt, &live[0], copy, n);
    free(copy);
    return ok ? OT_NIL : 0;
  }
  if (id == B_READ_U8 || id == B_PEEK_U8) {
    int c = peek(rt, &live[0], 0);
    if (c == -2) return 0;
    if (c == -1) return OT_EOF;
    if (id == B_READ_U8) consume(live[0], 1);
    return ot_int(c);
  }
  if (id == B_READ_CHAR || id == B_PEEK_CHAR) {
    uint32_t cp;
    size_t width;
    int result = character(rt, &live[0], &cp, &width);
    if (result < 0) return result == -1 ? OT_EOF : 0;
    if (id == B_READ_CHAR) consume(live[0], width);
    return ((ot_value)cp << 8) | TAG_CHAR;
  }
  size_t limit = SIZE_MAX, start = 0, end = 0;
  if (id == B_READ_CHARACTERS || id == B_READ_BYTES) {
    if (!natural(live[1], &limit)) return fail(rt, "expected nonnegative count");
  }
  if (id == B_READ_BYTES_MUT) {
    if (!oti_kind(live[1], K_BYTEVECTOR)) return fail(rt, "expected bytevector");
    if (!range(rt, first + 2, count > 2 ? count - 2 : 0, oti_bytes_length(live[1]), &start, &end))
      return 0;
    limit = end - start;
  }
  bool scan = id >= B_READ_UNTIL;
  if (scan && !oti_kind(live[1], K_STRING)) return fail(rt, "expected character set");
  char* bytes = NULL;
  size_t n = 0, capacity = 0, items = 0;
  bool binary = id == B_READ_BYTES || id == B_READ_BYTES_MUT;
  while (items < limit) {
    if (!ot_poll_interrupt(rt)) {
      free(bytes);
      return 0;
    }
    uint32_t cp = 0;
    size_t width = 1;
    int result = binary ? peek(rt, &live[0], 0) : character(rt, &live[0], &cp, &width);
    if (result == -2) {
      free(bytes);
      return 0;
    }
    if (result == -1) break;
    if (binary) cp = (uint32_t)result;
    if (scan) {
      bool found = false;
      size_t len = oti_bytes_length(live[1]);
      const char* set = (char*)oti_bytes_data(live[1]);
      for (size_t i = 0; i < len;) {
        uint32_t c;
        i = oti_utf8_next(set, len, i, &c);
        if (c == cp) found = true;
      }
      if (found == (id == B_READ_UNTIL)) break;
    }
    consume(live[0], width);
    items++;
    if (id == B_READ_LINE && cp == '\n') {
      if (n && bytes[n - 1] == '\r') n--;
      break;
    }
    if (id == B_READ_BYTES_MUT) {
      oti_bytes_data(live[1])[start + items - 1] = (unsigned char)cp;
      continue;
    }
    if (id == B_SKIP_WHILE) continue;
    char encoded[4];
    size_t used = binary ? 1 : oti_utf8_encode(cp, encoded);
    if (binary) encoded[0] = (char)cp;
    if (!oti_grow(rt, (void**)&bytes, &capacity, n + used, 1)) {
      free(bytes);
      return 0;
    }
    memcpy(bytes + n, encoded, used);
    n += used;
  }
  if (id == B_READ_BYTES_MUT) return items || !limit ? ot_int((intptr_t)items) : OT_EOF;
  if (id == B_SKIP_WHILE) return ot_int((intptr_t)items);
  if (!items && limit && (id == B_READ_LINE || id == B_READ_CHARACTERS || id == B_READ_BYTES)) {
    free(bytes);
    return OT_EOF;
  }
  ot_value result;
  if (id == B_READ_BYTES) {
    result = oti_bytes_new(rt, n);
    if (result && n) memcpy(oti_bytes_data(result), bytes, n);
  } else result = oti_text(rt, K_STRING, bytes ? bytes : "", n);
  free(bytes);
  return result;
}

ot_value oti_port_builtin(ot_runtime* rt, unsigned id, size_t first, size_t count) {
  ot_value live[2] = {OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  ot_value result = port_builtin(rt, id, first, count, live);
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

bool ot_flush_ports(ot_runtime* rt) {
  if (!oti_host_entry(rt)) return false;
  if (rt->frame_count || rt->evaluating || rt->compiling || rt->eval_depth)
    return oti_error(rt, "port flushing requires an idle runtime");
  ot_value saved[] = {rt->condition_values[0], rt->restart_transfer[0], rt->restart_transfer[1]};
  oti_roots state_root;
  oti_roots_push(&rt->heap, &state_root, saved, 3);
  char error[sizeof(rt->error)];
  memcpy(error, rt->error, sizeof(error));
  const char* code = rt->error_code;
  size_t offset = rt->error_offset;
  bool raised = rt->raised, signaled = rt->signaled, restart = rt->restart_pending;
  bool exiting = rt->exit_pending, interrupted = rt->interrupted;
  int status = rt->exit_status;
  rt->error[0] = 0;
  rt->error_code = NULL;
  rt->raised = rt->signaled = rt->restart_pending = rt->exit_pending = rt->interrupted = false;
  rt->condition_values[0] = rt->restart_transfer[0] = rt->restart_transfer[1] = OT_NIL;
  size_t count = rt->open_port_count;
  ot_value* ports = count ? malloc(count * sizeof(ot_value)) : NULL;
  bool ok = !count || ports;
  if (!ok) oti_error(rt, "out of host memory");
  else {
    if (count) memcpy(ports, rt->open_ports, count * sizeof(ot_value));
    oti_roots roots;
    oti_roots_push(&rt->heap, &roots, ports, count);
    for (size_t i = 0; ok && i < count; i++) {
      if (!ot_poll_interrupt(rt)) {
        ok = false;
        break;
      }
      if (!info(ports[i])->input && !info(ports[i])->closed) ok = flush(rt, &ports[i]);
    }
    oti_roots_pop(&rt->heap, &roots);
  }
  free(ports);
  if (ok) {
    memcpy(rt->error, error, sizeof(error));
    rt->error_code = code;
    rt->error_offset = offset;
    rt->raised = raised;
    rt->signaled = signaled;
    rt->restart_pending = restart;
    rt->exit_pending = exiting;
    rt->exit_status = status;
    rt->interrupted = interrupted;
    rt->condition_values[0] = saved[0];
    rt->restart_transfer[0] = saved[1];
    rt->restart_transfer[1] = saved[2];
  }
  oti_roots_pop(&rt->heap, &state_root);
  return ok;
}
