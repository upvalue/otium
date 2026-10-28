#include "internal.h"
#include <errno.h>
#include <limits.h>
#ifndef OTIUM_FREESTANDING
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#include <stdlib.h>
#include <string.h>

/* buffer holds the port's bytes. transfer is the :read or :write callback of
 * a custom port; name is the port name or file path. kind is 0 for string,
 * 1 for bytevector, 2 for standard, 3 for custom and 4 for file ports. */
typedef struct {
  oti_object_header header;
  ot_value buffer, transfer, flush, close, name;
  size_t position, length, capacity, offset;
  unsigned kind;
  bool input, closed, eof, busy;
  ot_io_host host;
  void* userdata;
  intptr_t handle;
} oti_port_obj;

OTI_LAYOUT(oti_port_obj, 5, position);

static inline oti_port_obj* port_object(ot_value p) { return OTI_CAST(oti_port_obj, K_PORT, p); }

void oti_port_repr(FILE* out, ot_value p) {
  if (port_object(p)->kind == 3 || port_object(p)->kind == 4) {
    fputs("#<port ", out);
    ot_write(NULL, out, port_object(p)->name);
    fprintf(out, " %s>", port_object(p)->input ? "input" : "output");
    return;
  }
  const char* name = port_object(p)->kind == 0     ? "\"string\""
                     : port_object(p)->kind == 1   ? "\"bytevector\""
                     : port_object(p)->handle == 0 ? "stdin"
                     : port_object(p)->handle == 1 ? "stdout"
                                                   : "stderr";
  fprintf(out, "#<port %s %s>", name, port_object(p)->input ? "input" : "output");
}

#ifndef OTIUM_FREESTANDING
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

#else
/* Without an operating system the host must install an I/O host; until then
 * every call fails. */
static int posix_open(void* userdata, const char* path, unsigned flags, intptr_t* handle) {
  (void)userdata;
  (void)path;
  (void)flags;
  (void)handle;
  return ENOSYS;
}

static long posix_read(void* userdata, intptr_t handle, uint8_t* bytes, size_t length) {
  (void)userdata;
  (void)handle;
  (void)bytes;
  (void)length;
  return -ENOSYS;
}

static long posix_write(void* userdata, intptr_t handle, const uint8_t* bytes, size_t length) {
  (void)userdata;
  (void)handle;
  (void)bytes;
  (void)length;
  return -ENOSYS;
}

static int posix_close(void* userdata, intptr_t handle) {
  (void)userdata;
  (void)handle;
  return 0;
}
#endif

static const ot_io_host posix_host = {posix_open, posix_read, posix_write, posix_close, NULL};

bool ot_set_io_host(ot_state* rt, const ot_io_host* host, void* userdata) {
  if (!oti_host_entry(rt)) return false;
  if (host && (!host->open || !host->read || !host->write || !host->close))
    return oti_error(rt, "I/O host requires open, read, write and close callbacks");
  rt->io_host = host ? *host : posix_host;
  rt->io_host_set = host != NULL;
  rt->io_userdata = host ? userdata : NULL;
  return true;
}

static void port_host(ot_state* rt, ot_value port) {
  port_object(port)->host = rt->io_host_set ? rt->io_host : posix_host;
  port_object(port)->userdata = rt->io_host_set ? rt->io_userdata : NULL;
}

static void host_begin(ot_state* rt, ot_value port) {
  rt->host_io_active = true;
  oti_heap_set_ready(&rt->heap, rt->heap.used);
  port_object(port)->busy = true;
}

static bool host_end(ot_state* rt, ot_value port) {
  rt->host_io_active = false;
  port_object(port)->busy = false;
  if (!rt->host_io_reentry) return true;
  rt->host_io_reentry = false;
  return oti_error(rt, "host I/O callback cannot reenter Otium");
}

static bool register_port(ot_state* rt, ot_value p) {
  if (!oti_grow(rt, (void**)&rt->open_ports, &rt->open_port_capacity, rt->open_port_count + 1,
                sizeof(ot_value)))
    return false;
  rt->open_port_roots.slots = rt->open_ports;
  rt->open_ports[rt->open_port_count++] = p;
  rt->open_port_roots.count = rt->open_port_count;
  return true;
}

static void unregister_port(ot_state* rt, ot_value p) {
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
         oti_kind(v, K_NATIVE) || oti_collection(v);
}

/* The caller roots *port, and function is one of its callbacks. Never retain
 * the port object pointer across a callback. */
static ot_value callback(ot_state* rt, ot_value* port, ot_value function, bool bytes, size_t start,
                         size_t end) {
  if (function == OT_NIL) return OT_NIL;
  if (port_object(*port)->busy) return oti_fail(rt, "port callback reentry");
  ot_value v[] = {function, OT_EMPTY, port_object(*port)->buffer, ot_int((intptr_t)start),
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
      oti_as_pair(pair)->car = v[i + 1];
      oti_as_pair(pair)->cdr = v[1];
      v[1] = pair;
    }
  ot_value result = 0;
  if (ok) {
    port_object(*port)->busy = true;
    result = oti_call_list(rt, v[0], v[1]);
    port_object(*port)->busy = false;
  }
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static void host_error(ot_state* rt, ot_value* port, const char* op, int error) {
  if (!ot_poll_interrupt(rt)) return;
  if (port_object(*port)->kind == 4)
    oti_file_error(rt, op, port_object(*port)->name, error ? error : EIO);
  else oti_io_error(rt, ":other");
}

static long transfer(ot_state* rt, ot_value* port, bool writing, size_t start, size_t length) {
  /* A blocking read is a safe place to run an interrupt handler; a write may
   * be partway through the buffer. */
  bool (*poll)(ot_state*) = writing ? ot_poll_interrupt : oti_interrupt_point;
  for (;;) {
    if (!poll(rt)) return -1;
    oti_port_obj* p = port_object(*port);
    unsigned char* bytes = oti_bytes_data(port_object(*port)->buffer) + start;
    host_begin(rt, *port);
    long n = writing ? p->host.write(p->userdata, p->handle, bytes, length)
                     : p->host.read(p->userdata, p->handle, bytes, length);
    if (!host_end(rt, *port)) return -1;
    if (n == -EINTR) {
      if (!poll(rt)) return -1;
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

static bool flush(ot_state* rt, ot_value* port) {
  if (port_object(*port)->kind < 2) return true;
  while (port_object(*port)->offset < port_object(*port)->length) {
    size_t left = port_object(*port)->length - port_object(*port)->offset;
    size_t written;
    if (port_object(*port)->kind == 3) {
      ot_value n = callback(rt, port, port_object(*port)->transfer, true,
                            port_object(*port)->offset, port_object(*port)->length);
      if (!n) return false;
      if (!oti_natural(n, &written) || !written || written > left)
        return oti_error(rt, "invalid custom port write count");
    } else {
      long n = transfer(rt, port, true, port_object(*port)->offset, left);
      if (n < 0) return false;
      written = (size_t)n;
    }
    port_object(*port)->offset += written;
  }
  port_object(*port)->offset = port_object(*port)->length = 0;
  if (port_object(*port)->kind == 3)
    return callback(rt, port, port_object(*port)->flush, false, 0, 0) != 0;
  oti_port_obj* p = port_object(*port);
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

static ot_value make_custom(ot_state* rt, ot_value* port, size_t first, size_t count, bool input) {
  if (count != 2 || !oti_kind(rt->stack[first], K_STRING) ||
      !oti_kind(rt->stack[first + 1], K_TABLE))
    return oti_fail(rt, "custom port needs a name and callback table");
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
      oti_fail(rt, "invalid custom port callback");
      ok = false;
      break;
    }
  }
  if (ok) {
    *port = oti_alloc(rt, K_PORT, 5, OTI_BYTES(oti_port_obj, 5));
    ok = *port != 0;
    if (ok) {
      port_object(*port)->input = input;
      port_object(*port)->kind = 3;
      port_object(*port)->capacity = 4096;
      port_object(*port)->transfer = callbacks[0];
      port_object(*port)->flush = callbacks[1];
      port_object(*port)->close = callbacks[2];
      port_object(*port)->name = rt->stack[first];
      ot_value buffer = oti_bytes_new(rt, 4096);
      ok = buffer != 0;
      if (ok) {
        port_object(*port)->buffer = buffer;
        ok = register_port(rt, *port);
      }
    }
  }
  oti_roots_pop(&rt->heap, &roots);
  return ok ? *port : 0;
}

static ot_value open_file(ot_state* rt, ot_value* port, size_t first, size_t count, bool input) {
  if (count < 1 || count > 2 || !oti_kind(rt->stack[first], K_STRING) ||
      (count == 2 && !oti_kind(rt->stack[first + 1], K_TABLE)))
    return oti_fail(rt, "file port needs a path and optional options table");
  size_t size = 4096;
  unsigned flags = input ? OT_IO_READ : OT_IO_WRITE | OT_IO_CREATE | OT_IO_TRUNCATE;
  if (count == 2) {
    ot_value key = oti_intern(rt, K_KEYWORD, ":buffer-size", 12);
    if (!key) return 0;
    ot_value option = oti_table_get(rt, rt->stack[first + 1], key, OT_NIL);
    if (!option) return 0;
    if (option != OT_NIL && (!oti_natural(option, &size) || !size))
      return oti_fail(rt, "invalid port buffer size");
    if (!input) {
      key = oti_intern(rt, K_KEYWORD, ":if-exists", 10);
      if (!key) return 0;
      option = oti_table_get(rt, rt->stack[first + 1], key, OT_NIL);
      if (!option) return 0;
      if (option != OT_NIL) {
        if (!oti_kind(option, K_KEYWORD)) return oti_fail(rt, "invalid if-exists option");
        const char* name = (char*)oti_bytes_data(option);
        if (!strcmp(name, ":append")) {
          flags = OT_IO_WRITE | OT_IO_CREATE | OT_IO_APPEND;
        } else if (!strcmp(name, ":error")) flags = OT_IO_WRITE | OT_IO_CREATE | OT_IO_EXCLUSIVE;
        else if (strcmp(name, ":truncate")) return oti_fail(rt, "invalid if-exists option");
      }
    }
  }
  if (size < 4) size = 4;
  size_t length = oti_bytes_length(rt->stack[first]);
  if (memchr(oti_bytes_data(rt->stack[first]), 0, length))
    return oti_fail(rt, "file path contains NUL");
  *port = oti_alloc(rt, K_PORT, 5, OTI_BYTES(oti_port_obj, 5));
  if (!*port) return 0;
  port_object(*port)->name = rt->stack[first];
  ot_value buffer = oti_bytes_new(rt, size);
  if (!buffer) return 0;
  port_object(*port)->buffer = buffer;
  port_object(*port)->kind = 4;
  port_object(*port)->input = input;
  port_object(*port)->capacity = size;
  port_host(rt, *port);
  oti_port_obj* p = port_object(*port);
  host_begin(rt, *port);
  int error =
      p->host.open(p->userdata, (char*)oti_bytes_data(port_object(*port)->name), flags, &p->handle);
  bool entered = host_end(rt, *port);
  if (error) {
    if (!entered) return 0;
    return oti_file_error(rt, input ? "open-input-file" : "open-output-file",
                          port_object(*port)->name, error);
  }
  if (!entered || !register_port(rt, *port)) {
    host_begin(rt, *port);
    p->host.close(p->userdata, p->handle);
    host_end(rt, *port);
    port_object(*port)->closed = true;
    return 0;
  }
  return *port;
}

/* Destruction invokes no Otium code. Native hosts may drain buffered output;
 * failures are ignored here, so embedders should call ot_flush_ports first. */
void oti_ports_destroy(ot_state* rt) {
  for (size_t i = 0; i < rt->open_port_count; i++) {
    ot_value port = rt->open_ports[i];
    oti_port_obj* p = port_object(port);
    if ((p->kind != 2 && p->kind != 4) || p->closed) continue;
    host_begin(rt, port);
    if (!p->input) {
      while (p->offset < p->length) {
        size_t left = p->length - p->offset;
        long n = p->host.write(p->userdata, p->handle,
                               oti_bytes_data(port_object(port)->buffer) + p->offset, left);
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

static ot_value standard(ot_state* rt, unsigned which) {
  if (rt->standard_ports[which] == OT_NIL) {
    ot_value p = oti_alloc(rt, K_PORT, 5, OTI_BYTES(oti_port_obj, 5));
    if (!p) return 0;
    oti_roots roots;
    oti_roots_push(&rt->heap, &roots, &p, 1);
    port_object(p)->kind = 2;
    port_object(p)->input = which == 0;
    port_object(p)->handle = (intptr_t)which;
    port_object(p)->capacity = 4096;
    port_host(rt, p);
    ot_value buffer = oti_bytes_new(rt, 4096);
    bool ok = buffer != 0;
    if (ok) {
      port_object(p)->buffer = buffer;
      ok = register_port(rt, p);
    }
    if (ok) rt->standard_ports[which] = p;
    oti_roots_pop(&rt->heap, &roots);
    if (!ok) return 0;
  }
  return rt->standard_ports[which];
}

static ot_value current_port(ot_state* rt, unsigned which) {
  const char* name = which == 0   ? "current-input-port"
                     : which == 1 ? "current-output-port"
                                  : "current-error-port";
  uint32_t index = oti_binding_global(rt, 0, name, strlen(name), false);
  if (index == UINT32_MAX || oti_unbound(oti_global_value(rt, index))) return standard(rt, which);
  ot_value p = oti_global_value(rt, index);
  if (!oti_kind(p, K_PARAM)) return oti_fail(rt, "current port binding must be a param");
  return oti_param_call(rt, p, 0);
}

static ot_value current_standard_port(ot_state* rt, bool input) {
  return current_port(rt, input ? 0 : 1);
}

static bool check(ot_state* rt, ot_value p, bool input) {
  if (!oti_kind(p, K_PORT) || port_object(p)->input != input)
    return oti_error(rt, input ? "expected input port" : "expected output port");
  if (port_object(p)->busy) return oti_error(rt, "port callback reentry");
  if (port_object(p)->closed) {
    oti_io_error(rt, ":closed");
    return false;
  }
  return true;
}

static int peek(ot_state* rt, ot_value* port, size_t ahead) {
  oti_port_obj* s = port_object(*port);
  if (s->kind < 2)
    return ahead < s->length - s->position
               ? oti_bytes_data(port_object(*port)->buffer)[s->position + ahead]
               : -1;
  while (port_object(*port)->length - port_object(*port)->offset <= ahead) {
    s = port_object(*port);
    if (s->eof) return -1;
    size_t remaining = s->length - s->offset;
    unsigned char* data = oti_bytes_data(port_object(*port)->buffer);
    memmove(data, data + s->offset, remaining);
    s->offset = 0;
    s->length = remaining;
    size_t available = s->capacity - remaining, n;
    if (s->kind == 3) {
      ot_value result = callback(rt, port, s->transfer, true, remaining, s->capacity);
      if (!result) return -2;
      if (result == OT_EOF) {
        port_object(*port)->eof = true;
        return -1;
      }
      if (!oti_natural(result, &n) || !n || n > available) {
        oti_fail(rt, "invalid custom port read count");
        return -2;
      }
    } else {
      long result = transfer(rt, port, false, remaining, available);
      if (result < 0) return -2;
      if (!result) {
        port_object(*port)->eof = true;
        return -1;
      }
      n = (size_t)result;
    }
    port_object(*port)->length += n;
  }
  return oti_bytes_data(port_object(*port)->buffer)[port_object(*port)->offset + ahead];
}

static void consume(ot_value p, size_t n) {
  oti_port_obj* s = port_object(p);
  s->position += n;
  if (s->kind >= 2) s->offset += n;
}

static int port_character(ot_state* rt, ot_value* port, uint32_t* cp, size_t* width) {
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
    oti_decode_error(rt, port_object(*port)->position);
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
static ot_value read_datum(ot_state* rt, ot_value* port) {
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
    int state = port_character(rt, port, &cp, &width);
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
      return oti_fail(rt, "port datum exceeds reader limit");
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

static bool append(ot_state* rt, ot_value* port, const char* bytes, size_t n) {
  if (!check(rt, (*port), false)) return false;
  if (port_object(*port)->kind >= 2) {
    while (n) {
      if (port_object(*port)->length == port_object(*port)->capacity && !flush(rt, port))
        return false;
      size_t room = port_object(*port)->capacity - port_object(*port)->length;
      size_t take = n < room ? n : room;
      memcpy(oti_bytes_data(port_object(*port)->buffer) + port_object(*port)->length, bytes, take);
      port_object(*port)->length += take;
      bytes += take;
      n -= take;
      if (port_object(*port)->length == port_object(*port)->capacity && !flush(rt, port))
        return false;
    }
    return port_object(*port)->kind != 2 || flush(rt, port);
  }
  size_t length = port_object(*port)->length;
  if (n > SIZE_MAX - length) return oti_error(rt, "port output too large");
  if (length + n > port_object(*port)->capacity) {
    size_t capacity = port_object(*port)->capacity;
    if (capacity > SIZE_MAX / 2) capacity = length + n;
    else capacity *= 2;
    if (capacity < 64) capacity = 64;
    if (capacity < length + n) capacity = length + n;
    ot_value data = oti_alloc(rt, K_BUFFER_DATA, 0, capacity);
    if (data) {
      if (length)
        memcpy(oti_as_buffer_data(data)->bytes,
               oti_as_buffer_data(port_object(*port)->buffer)->bytes, length);
      port_object(*port)->buffer = data;
      port_object(*port)->capacity = capacity;
    }
    if (!data) return false;
  }
  if (n) memcpy(oti_as_buffer_data(port_object(*port)->buffer)->bytes + length, bytes, n);
  port_object(*port)->length += n;
  return true;
}

static ot_value port_print_values(ot_state* rt, ot_value p, size_t first, size_t count,
                                  bool write) {
  if (!p || !check(rt, p, false)) return 0;
  char* bytes = NULL;
  size_t length = 0;
  FILE* stream = open_memstream(&bytes, &length);
  if (!stream) return oti_fail(rt, "out of host memory");
  for (size_t i = 0; i < count; i++) {
    if (i) fputc(' ', stream);
    if (write) ot_write(rt, stream, rt->stack[first + i]);
    else ot_print(rt, stream, rt->stack[first + i]);
  }
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

/* display-to and write-to */
static ot_value port_print_to(ot_state* rt, size_t first, size_t count, bool write) {
  if (!count) return oti_fail(rt, "output needs a port");
  return port_print_values(rt, rt->stack[first], first + 1, count - 1, write);
}

static bool range(ot_state* rt, size_t first, size_t count, size_t n, size_t* start, size_t* end) {
  *start = 0;
  *end = n;
  if ((count && !oti_natural(rt->stack[first], start)) ||
      (count > 1 && !oti_natural(rt->stack[first + 1], end)) || *start > *end || *end > n)
    return oti_error(rt, "invalid port slice");
  return true;
}

static ot_value port_open_ports(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (count) return oti_fail(rt, "open-ports takes no arguments");
  size_t base = rt->sp, n = rt->open_port_count;
  if (n > rt->stack_limit - base || !oti_stack(rt, base + n))
    return oti_fail(rt, "VM stack limit exceeded");
  if (n) memcpy(rt->stack + base, rt->open_ports, n * sizeof(ot_value));
  rt->sp = rt->stack_roots.count = base + n;
  ot_value result = oti_array_from(rt, base, n);
  rt->sp = rt->stack_roots.count = base;
  return result;
}

/* %standard-input, %standard-output and %standard-error */
static ot_value port_standard(ot_state* rt, size_t count, unsigned which) {
  if (count) return oti_fail(rt, "standard port takes no arguments");
  return standard(rt, which);
}

/* port?, input-port?, output-port?, input-port-open? and output-port-open? */
static ot_value port_predicate(ot_state* rt, size_t first, size_t count, bool directed, bool input,
                               bool open) {
  if (count != 1) return oti_fail(rt, "port predicate takes one argument");
  ot_value p = rt->stack[first];
  bool yes = oti_kind(p, K_PORT);
  if (yes && directed) yes = port_object(p)->input == input;
  if (yes && open) yes = !port_object(p)->closed;
  return yes ? OT_TRUE : OT_FALSE;
}

/* open-input-string, open-input-bytevector, open-output-string and open-output-bytevector */
static ot_value port_open_memory(ot_state* rt, size_t first, size_t count, ot_value live[2],
                                 bool input, bool string) {
  if (count != (input ? 1u : 0u)) return oti_fail(rt, "wrong number of port constructor arguments");
  size_t n = 0;
  if (input) {
    if (!oti_kind(rt->stack[first], string ? K_STRING : K_BYTEVECTOR))
      return oti_fail(rt, "invalid input source");
    n = oti_bytes_length(rt->stack[first]);
  }
  live[0] = oti_alloc(rt, K_PORT, 5, OTI_BYTES(oti_port_obj, 5));
  if (!live[0]) return 0;
  port_object(live[0])->input = input;
  port_object(live[0])->kind = string ? 0 : 1;
  port_object(live[0])->length = n;
  if (input) {
    ot_value bytes = oti_bytes_new(rt, n);
    if (bytes) {
      if (n) memcpy(oti_bytes_data(bytes), oti_bytes_data(rt->stack[first]), n);
      port_object(live[0])->buffer = bytes;
    }
    if (!bytes) return 0;
  }
  return live[0];
}

static ot_value port_close(ot_state* rt, size_t first, size_t count, ot_value live[2]) {
  if (count != 1 || !oti_kind(rt->stack[first], K_PORT)) return oti_fail(rt, "close needs a port");
  live[0] = rt->stack[first];
  if (port_object(live[0])->closed) return OT_NIL;
  if (port_object(live[0])->busy) return oti_fail(rt, "port callback reentry");
  if (!port_object(live[0])->input && !flush(rt, &live[0])) return 0;
  if (port_object(live[0])->kind == 3 &&
      !callback(rt, &live[0], port_object(live[0])->close, false, 0, 0))
    return 0;
  int error = 0;
  if (port_object(live[0])->kind == 4) {
    oti_port_obj* p = port_object(live[0]);
    host_begin(rt, live[0]);
    error = p->host.close(p->userdata, p->handle);
    bool entered = host_end(rt, live[0]);
    port_object(live[0])->closed = true;
    unregister_port(rt, live[0]);
    if (!entered) return 0;
    return error ? oti_file_error(rt, "close-port", port_object(live[0])->name, error) : OT_NIL;
  }
  port_object(live[0])->closed = true;
  unregister_port(rt, live[0]);
  return error ? oti_file_error(rt, "close-port", port_object(live[0])->name, error) : OT_NIL;
}

/* get-output-string and get-output-bytevector */
static ot_value port_get_output(ot_state* rt, size_t first, size_t count, ot_value live[2],
                                bool string) {
  if (count != 1) return oti_fail(rt, "output extraction needs a port");
  live[0] = rt->stack[first];
  if (!check(rt, live[0], false)) return 0;
  if (port_object(live[0])->kind != (string ? 0u : 1u))
    return oti_fail(rt, "wrong output port kind");
  size_t n = port_object(live[0])->length;
  char* copy = malloc(n + 1);
  if (!copy) return oti_fail(rt, "out of host memory");
  if (n) memcpy(copy, oti_as_buffer_data(port_object(live[0])->buffer)->bytes, n);
  ot_value result;
  if (string) {
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

static ot_value port_read_datum(ot_state* rt, size_t first, size_t count, ot_value live[2]) {
  if (count > 1) return oti_fail(rt, "read takes an optional port");
  live[0] = count ? rt->stack[first] : current_standard_port(rt, true);
  if (!live[0] || !check(rt, live[0], true)) return 0;
  return read_datum(rt, &live[0]);
}

/* Arguments for the read and write family: `min` leading arguments, the first kept in live[1],
   then an optional port, kept in live[0], then optional range bounds up to `max`. */
static bool port_arguments(ot_state* rt, size_t first, size_t count, ot_value live[2], size_t min,
                           size_t max, bool input) {
  if (count < min || count > max) {
    oti_fail(rt, "wrong number of port arguments");
    return false;
  }
  live[0] = count > min ? rt->stack[first + min] : current_standard_port(rt, input);
  if (!live[0] || !check(rt, live[0], input)) return false;
  live[1] = min ? rt->stack[first] : OT_NIL;
  return true;
}

static ot_value port_flush_output(ot_state* rt, size_t first, size_t count, ot_value live[2]) {
  if (!port_arguments(rt, first, count, live, 0, 1, false)) return 0;
  return flush(rt, &live[0]) ? OT_NIL : 0;
}

/* Copy first, because the source may be inside a heap object that moves while appending. */
static ot_value port_emit(ot_state* rt, ot_value live[2], const char* source, size_t n) {
  char* copy = malloc(n + 1);
  if (!copy) return oti_fail(rt, "out of host memory");
  memcpy(copy, source, n);
  bool ok = append(rt, &live[0], copy, n);
  free(copy);
  return ok ? OT_NIL : 0;
}

static ot_value port_write_u8(ot_state* rt, size_t first, size_t count, ot_value live[2]) {
  if (!port_arguments(rt, first, count, live, 1, 2, false)) return 0;
  size_t byte;
  if (!oti_natural(live[1], &byte) || byte > 255) return oti_fail(rt, "expected byte");
  char one = (char)byte;
  return port_emit(rt, live, &one, 1);
}

static ot_value port_write_char(ot_state* rt, size_t first, size_t count, ot_value live[2]) {
  if (!port_arguments(rt, first, count, live, 1, 2, false)) return 0;
  if ((live[1] & 255) != TAG_CHAR) return oti_fail(rt, "expected character");
  char one[4];
  size_t n = oti_utf8_encode((uint32_t)(live[1] >> 8), one);
  return port_emit(rt, live, one, n);
}

/* write-string and write-bytevector */
static ot_value port_write_sequence(ot_state* rt, size_t first, size_t count, ot_value live[2],
                                    bool string) {
  if (!port_arguments(rt, first, count, live, 1, 4, false)) return 0;
  if (!oti_kind(live[1], string ? K_STRING : K_BYTEVECTOR))
    return oti_fail(rt, "invalid output data");
  size_t length = oti_bytes_length(live[1]), items = length;
  const char* source = (char*)oti_bytes_data(live[1]);
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
  return port_emit(rt, live, source + start, end - start);
}

/* read-u8 and peek-u8 */
static ot_value port_read_u8(ot_state* rt, size_t first, size_t count, ot_value live[2],
                             bool advance) {
  if (!port_arguments(rt, first, count, live, 0, 1, true)) return 0;
  int c = peek(rt, &live[0], 0);
  if (c == -2) return 0;
  if (c == -1) return OT_EOF;
  if (advance) consume(live[0], 1);
  return ot_int(c);
}

/* read-char and peek-char */
static ot_value port_read_char(ot_state* rt, size_t first, size_t count, ot_value live[2],
                               bool advance) {
  if (!port_arguments(rt, first, count, live, 0, 1, true)) return 0;
  uint32_t cp;
  size_t width;
  int result = port_character(rt, &live[0], &cp, &width);
  if (result < 0) return result == -1 ? OT_EOF : 0;
  if (advance) consume(live[0], width);
  return ((ot_value)cp << 8) | TAG_CHAR;
}

typedef struct {
  bool binary; /* bytes rather than characters */
  bool line;   /* stop after a newline, dropping it and a preceding return */
  bool scan;   /* live[1] is a character set */
  bool until;  /* stop at a member of the set rather than at a non-member */
  bool fill;   /* store into the bytevector in live[1] from `start` */
  bool keep;   /* collect what is read into *out */
} port_reading;

/* Read up to `limit` items from the port in live[0]. On success the caller frees *out. */
static bool port_read_items(ot_state* rt, ot_value live[2], port_reading how, size_t limit,
                            size_t start, char** out, size_t* length, size_t* read) {
  char* bytes = NULL;
  size_t n = 0, capacity = 0, items = 0;
  while (items < limit) {
    if (!ot_poll_interrupt(rt)) {
      free(bytes);
      return false;
    }
    uint32_t cp = 0;
    size_t width = 1;
    int result = how.binary ? peek(rt, &live[0], 0) : port_character(rt, &live[0], &cp, &width);
    if (result == -2) {
      free(bytes);
      return false;
    }
    if (result == -1) break;
    if (how.binary) cp = (uint32_t)result;
    if (how.scan) {
      bool found = false;
      size_t len = oti_bytes_length(live[1]);
      const char* set = (char*)oti_bytes_data(live[1]);
      for (size_t i = 0; i < len;) {
        uint32_t c;
        i = oti_utf8_next(set, len, i, &c);
        if (c == cp) found = true;
      }
      if (found == how.until) break;
    }
    consume(live[0], width);
    items++;
    if (how.line && cp == '\n') {
      if (n && bytes[n - 1] == '\r') n--;
      break;
    }
    if (how.fill) {
      oti_bytes_data(live[1])[start + items - 1] = (unsigned char)cp;
      continue;
    }
    if (!how.keep) continue;
    char encoded[4];
    size_t used = how.binary ? 1 : oti_utf8_encode(cp, encoded);
    if (how.binary) encoded[0] = (char)cp;
    if (!oti_grow(rt, (void**)&bytes, &capacity, n + used, 1)) {
      free(bytes);
      return false;
    }
    memcpy(bytes + n, encoded, used);
    n += used;
  }
  *out = bytes;
  *length = n;
  *read = items;
  return true;
}

static ot_value port_read_string(ot_state* rt, char* bytes, size_t n) {
  ot_value result = oti_text(rt, K_STRING, bytes ? bytes : "", n);
  free(bytes);
  return result;
}

static ot_value port_read_line(ot_state* rt, size_t first, size_t count, ot_value live[2]) {
  if (!port_arguments(rt, first, count, live, 0, 1, true)) return 0;
  char* bytes;
  size_t n, items;
  port_reading how = {.line = true, .keep = true};
  if (!port_read_items(rt, live, how, SIZE_MAX, 0, &bytes, &n, &items)) return 0;
  if (!items) {
    free(bytes);
    return OT_EOF;
  }
  return port_read_string(rt, bytes, n);
}

static ot_value port_read_all(ot_state* rt, size_t first, size_t count, ot_value live[2]) {
  if (!port_arguments(rt, first, count, live, 0, 1, true)) return 0;
  char* bytes;
  size_t n, items;
  port_reading how = {.keep = true};
  if (!port_read_items(rt, live, how, SIZE_MAX, 0, &bytes, &n, &items)) return 0;
  return port_read_string(rt, bytes, n);
}

/* %read-characters and read-bytevector */
static ot_value port_read_counted(ot_state* rt, size_t first, size_t count, ot_value live[2],
                                  bool binary) {
  if (!port_arguments(rt, first, count, live, 1, 2, true)) return 0;
  size_t limit;
  if (!oti_natural(live[1], &limit)) return oti_fail(rt, "expected nonnegative count");
  char* bytes;
  size_t n, items;
  port_reading how = {.binary = binary, .keep = true};
  if (!port_read_items(rt, live, how, limit, 0, &bytes, &n, &items)) return 0;
  if (!items && limit) {
    free(bytes);
    return OT_EOF;
  }
  if (!binary) return port_read_string(rt, bytes, n);
  ot_value result = oti_bytes_new(rt, n);
  if (result && n) memcpy(oti_bytes_data(result), bytes, n);
  free(bytes);
  return result;
}

static ot_value port_read_bytes_mut(ot_state* rt, size_t first, size_t count, ot_value live[2]) {
  if (!port_arguments(rt, first, count, live, 1, 4, true)) return 0;
  if (!oti_kind(live[1], K_BYTEVECTOR)) return oti_fail(rt, "expected bytevector");
  size_t start, end;
  if (!range(rt, first + 2, count > 2 ? count - 2 : 0, oti_bytes_length(live[1]), &start, &end))
    return 0;
  size_t limit = end - start;
  char* bytes;
  size_t n, items;
  port_reading how = {.binary = true, .fill = true};
  if (!port_read_items(rt, live, how, limit, start, &bytes, &n, &items)) return 0;
  free(bytes);
  return items || !limit ? ot_int((intptr_t)items) : OT_EOF;
}

/* Arguments for read-until, read-while and skip-while. */
static bool port_character_set(ot_state* rt, size_t first, size_t count, ot_value live[2]) {
  if (!port_arguments(rt, first, count, live, 1, 2, true)) return false;
  if (!oti_kind(live[1], K_STRING)) {
    oti_fail(rt, "expected character set");
    return false;
  }
  return true;
}

/* read-until and read-while */
static ot_value port_read_set(ot_state* rt, size_t first, size_t count, ot_value live[2],
                              bool until) {
  if (!port_character_set(rt, first, count, live)) return 0;
  char* bytes;
  size_t n, items;
  port_reading how = {.scan = true, .until = until, .keep = true};
  if (!port_read_items(rt, live, how, SIZE_MAX, 0, &bytes, &n, &items)) return 0;
  return port_read_string(rt, bytes, n);
}

static ot_value port_skip_while(ot_state* rt, size_t first, size_t count, ot_value live[2]) {
  if (!port_character_set(rt, first, count, live)) return 0;
  char* bytes;
  size_t n, items;
  port_reading how = {.scan = true};
  if (!port_read_items(rt, live, how, SIZE_MAX, 0, &bytes, &n, &items)) return 0;
  free(bytes);
  return ot_int((intptr_t)items);
}

static ot_value port_display_to_builtin(ot_state* rt, size_t first, size_t count) {
  return port_print_to(rt, first, count, false);
}

static ot_value port_write_to_builtin(ot_state* rt, size_t first, size_t count) {
  return port_print_to(rt, first, count, true);
}

static ot_value port_p_builtin(ot_state* rt, size_t first, size_t count) {
  return port_predicate(rt, first, count, false, false, false);
}

static ot_value port_input_p_builtin(ot_state* rt, size_t first, size_t count) {
  return port_predicate(rt, first, count, true, true, false);
}

static ot_value port_output_p_builtin(ot_state* rt, size_t first, size_t count) {
  return port_predicate(rt, first, count, true, false, false);
}

static ot_value port_input_open_builtin(ot_state* rt, size_t first, size_t count) {
  return port_predicate(rt, first, count, true, true, true);
}

static ot_value port_output_open_builtin(ot_state* rt, size_t first, size_t count) {
  return port_predicate(rt, first, count, true, false, true);
}

static ot_value port_stdin_builtin(ot_state* rt, size_t first, size_t count) {
  (void)first;
  return port_standard(rt, count, 0);
}

static ot_value port_stdout_builtin(ot_state* rt, size_t first, size_t count) {
  (void)first;
  return port_standard(rt, count, 1);
}

static ot_value port_stderr_builtin(ot_state* rt, size_t first, size_t count) {
  (void)first;
  return port_standard(rt, count, 2);
}

/* Define a builtin that runs `call` with two rooted slots in `live`. */
#define PORT_LIVE(name, call)                                                                      \
  static ot_value name(ot_state* rt, size_t first, size_t count) {                                 \
    ot_value live[2] = {OT_NIL, OT_NIL};                                                           \
    oti_roots roots;                                                                               \
    oti_roots_push(&rt->heap, &roots, live, 2);                                                    \
    ot_value result = call;                                                                        \
    oti_roots_pop(&rt->heap, &roots);                                                              \
    return result;                                                                                 \
  }

PORT_LIVE(port_open_input_string, port_open_memory(rt, first, count, live, true, true))
PORT_LIVE(port_open_output_string, port_open_memory(rt, first, count, live, false, true))
PORT_LIVE(port_open_input_bytes, port_open_memory(rt, first, count, live, true, false))
PORT_LIVE(port_open_output_bytes, port_open_memory(rt, first, count, live, false, false))
PORT_LIVE(port_get_output_string, port_get_output(rt, first, count, live, true))
PORT_LIVE(port_get_output_bytes, port_get_output(rt, first, count, live, false))
PORT_LIVE(port_close_port, port_close(rt, first, count, live))
PORT_LIVE(port_read_u8_builtin, port_read_u8(rt, first, count, live, true))
PORT_LIVE(port_peek_u8_builtin, port_read_u8(rt, first, count, live, false))
PORT_LIVE(port_read_char_builtin, port_read_char(rt, first, count, live, true))
PORT_LIVE(port_peek_char_builtin, port_read_char(rt, first, count, live, false))
PORT_LIVE(port_read_line_builtin, port_read_line(rt, first, count, live))
PORT_LIVE(port_read_all_builtin, port_read_all(rt, first, count, live))
PORT_LIVE(port_read_characters_builtin, port_read_counted(rt, first, count, live, false))
PORT_LIVE(port_read_bytes_builtin, port_read_counted(rt, first, count, live, true))
PORT_LIVE(port_read_bytes_mut_builtin, port_read_bytes_mut(rt, first, count, live))
PORT_LIVE(port_write_u8_builtin, port_write_u8(rt, first, count, live))
PORT_LIVE(port_write_char_builtin, port_write_char(rt, first, count, live))
PORT_LIVE(port_write_string_builtin, port_write_sequence(rt, first, count, live, true))
PORT_LIVE(port_write_bytes_builtin, port_write_sequence(rt, first, count, live, false))
PORT_LIVE(port_flush_builtin, port_flush_output(rt, first, count, live))
PORT_LIVE(port_read_until_builtin, port_read_set(rt, first, count, live, true))
PORT_LIVE(port_read_while_builtin, port_read_set(rt, first, count, live, false))
PORT_LIVE(port_skip_while_builtin, port_skip_while(rt, first, count, live))
PORT_LIVE(port_read_datum_builtin, port_read_datum(rt, first, count, live))
PORT_LIVE(port_make_input_builtin, make_custom(rt, &live[0], first, count, true))
PORT_LIVE(port_make_output_builtin, make_custom(rt, &live[0], first, count, false))
PORT_LIVE(port_open_input_file_builtin, open_file(rt, &live[0], first, count, true))
PORT_LIVE(port_open_output_file_builtin, open_file(rt, &live[0], first, count, false))

#undef PORT_LIVE

const oti_builtin_def oti_port_builtins[] = {
    {B_PORT_P, "port?", port_p_builtin},
    {B_INPUT_P, "input-port?", port_input_p_builtin},
    {B_OUTPUT_P, "output-port?", port_output_p_builtin},
    {B_INPUT_OPEN, "input-port-open?", port_input_open_builtin},
    {B_OUTPUT_OPEN, "output-port-open?", port_output_open_builtin},
    {B_OPEN_INPUT_STRING, "open-input-string", port_open_input_string},
    {B_OPEN_OUTPUT_STRING, "open-output-string", port_open_output_string},
    {B_GET_OUTPUT_STRING, "get-output-string", port_get_output_string},
    {B_OPEN_INPUT_BYTES, "open-input-bytevector", port_open_input_bytes},
    {B_OPEN_OUTPUT_BYTES, "open-output-bytevector", port_open_output_bytes},
    {B_GET_OUTPUT_BYTES, "get-output-bytevector", port_get_output_bytes},
    {B_CLOSE_PORT, "close-port", port_close_port},
    {B_READ_U8, "read-u8", port_read_u8_builtin},
    {B_PEEK_U8, "peek-u8", port_peek_u8_builtin},
    {B_READ_CHAR, "read-char", port_read_char_builtin},
    {B_PEEK_CHAR, "peek-char", port_peek_char_builtin},
    {B_READ_LINE, "read-line", port_read_line_builtin},
    {B_READ_ALL, "read-all", port_read_all_builtin},
    {B_READ_CHARACTERS, "%read-characters", port_read_characters_builtin},
    {B_READ_BYTES, "read-bytevector", port_read_bytes_builtin},
    {B_READ_BYTES_MUT, "read-bytevector!", port_read_bytes_mut_builtin},
    {B_WRITE_U8, "write-u8", port_write_u8_builtin},
    {B_WRITE_CHAR, "write-char", port_write_char_builtin},
    {B_WRITE_STRING, "write-string", port_write_string_builtin},
    {B_WRITE_BYTES, "write-bytevector", port_write_bytes_builtin},
    {B_FLUSH_PORT, "flush-output-port", port_flush_builtin},
    {B_DISPLAY_TO, "display-to", port_display_to_builtin},
    {B_WRITE_TO, "write-to", port_write_to_builtin},
    {B_READ_UNTIL, "read-until", port_read_until_builtin},
    {B_READ_WHILE, "read-while", port_read_while_builtin},
    {B_SKIP_WHILE, "skip-while", port_skip_while_builtin},
    {B_READ_DATUM, "read", port_read_datum_builtin},
    {B_MAKE_INPUT_PORT, "make-input-port", port_make_input_builtin},
    {B_MAKE_OUTPUT_PORT, "make-output-port", port_make_output_builtin},
    {B_OPEN_PORTS, "open-ports", port_open_ports},
    {B_STDIN, "%standard-input", port_stdin_builtin},
    {B_STDOUT, "%standard-output", port_stdout_builtin},
    {B_STDERR, "%standard-error", port_stderr_builtin},
    {B_OPEN_INPUT_FILE, "open-input-file", port_open_input_file_builtin},
    {B_OPEN_OUTPUT_FILE, "open-output-file", port_open_output_file_builtin},
    {0, NULL, NULL},
};

bool ot_flush_ports(ot_state* rt) {
  if (!oti_host_entry(rt)) return false;
  if (rt->frame_count || rt->evaluating || rt->compiling || rt->eval_depth)
    return oti_error(rt, "port flushing requires an idle runtime");
  ot_value saved[] = {rt->condition_values[0], rt->restart_transfer[0], rt->restart_transfer[1]};
  oti_roots state_root;
  oti_roots_push(&rt->heap, &state_root, saved, 3);
  oti_error_record error;
  oti_roots error_roots;
  oti_take_error(rt, &error, &error_roots);
  bool raised = rt->raised, signaled = rt->signaled, restart = rt->restart_pending;
  bool exiting = rt->exit_pending, interrupted = rt->interrupted;
  int status = rt->exit_status;
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
      if (!port_object(ports[i])->input && !port_object(ports[i])->closed)
        ok = flush(rt, &ports[i]);
    }
    oti_roots_pop(&rt->heap, &roots);
  }
  free(ports);
  if (ok) {
    oti_restore_error(rt, &error, &error_roots);
    rt->raised = raised;
    rt->signaled = signaled;
    rt->restart_pending = restart;
    rt->exit_pending = exiting;
    rt->exit_status = status;
    rt->interrupted = interrupted;
    rt->condition_values[0] = saved[0];
    rt->restart_transfer[0] = saved[1];
    rt->restart_transfer[1] = saved[2];
  } else oti_drop_error(rt, &error, &error_roots);
  oti_roots_pop(&rt->heap, &state_root);
  return ok;
}
