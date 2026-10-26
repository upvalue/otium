#include "internal.h"
#include <assert.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
  ot_runtime* rt;
  unsigned char input[128], output[1024];
  size_t input_length, positions[128], output_length, opens, reads, writes, closes, flushes;
  unsigned flags;
  int read_error, write_error, close_error, flush_error;
  size_t fail_after;
  bool reenter, invalid_read, invalid_write, interrupt;
  volatile sig_atomic_t flag;
} Host;

static void managed(Host* h, const void* bytes, size_t length) {
  uintptr_t start = (uintptr_t)h->rt->heap.space[h->rt->heap.active];
  assert((uintptr_t)bytes >= start && (uintptr_t)bytes + length <= start + h->rt->heap.used);
  if (h->reenter) {
    size_t collections = ot_collections(h->rt);
    ot_value result;
    assert(!ot_eval(h->rt, "(+ 1 2)", 7, &result));
    assert(!ot_call(h->rt, "+", NULL, 0, &result));
    assert(!ot_set_heap_limit(h->rt, 16 * 1024 * 1024));
    assert(!ot_flush_ports(h->rt));
    assert(!ot_set_io_host(h->rt, NULL, NULL));
    ot_destroy(h->rt);
    assert(ot_collections(h->rt) == collections);
  }
}

static int host_open(void* userdata, const char* path, unsigned flags, intptr_t* handle) {
  Host* h = userdata;
  managed(h, path, strlen(path));
  if (!strcmp(path, "missing")) return ENOENT;
  h->flags = flags;
  *handle = (intptr_t)(3 + h->opens++);
  assert(*handle < 128);
  return 0;
}

static long host_read(void* userdata, intptr_t handle, uint8_t* bytes, size_t length) {
  Host* h = userdata;
  managed(h, bytes, length);
  h->reads++;
  if (h->invalid_read) return (long)length + 1;
  if (h->read_error) {
    int error = h->read_error;
    h->read_error = 0;
    if (h->interrupt) h->flag = 1;
    return -error;
  }
  if (h->positions[handle] == h->input_length) return 0;
  assert(length);
  bytes[0] = h->input[h->positions[handle]++];
  return 1;
}

static long host_write(void* userdata, intptr_t handle, const uint8_t* bytes, size_t length) {
  (void)handle;
  Host* h = userdata;
  managed(h, bytes, length);
  h->writes++;
  if (h->invalid_write) return 0;
  if (h->write_error && h->output_length >= h->fail_after) {
    int error = h->write_error;
    h->write_error = 0;
    return -error;
  }
  size_t take = length < 2 ? length : 2;
  assert(h->output_length + take < sizeof(h->output));
  memcpy(h->output + h->output_length, bytes, take);
  h->output_length += take;
  return (long)take;
}

static int host_close(void* userdata, intptr_t handle) {
  Host* h = userdata;
  assert(handle >= 3);
  h->closes++;
  int error = h->close_error;
  h->close_error = 0;
  return error;
}

static int host_flush(void* userdata, intptr_t handle) {
  (void)handle;
  Host* h = userdata;
  h->flushes++;
  int error = h->flush_error;
  h->flush_error = 0;
  return error;
}

static ot_value eval(ot_runtime* rt, const char* source) {
  ot_value result;
  if (!ot_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s\n%s\n", source, ot_error(rt));
    abort();
  }
  return result;
}

static void check(ot_runtime* rt, const char* source) {
  ot_value result = eval(rt, source);
  if (result != OT_TRUE) {
    fprintf(stderr, "%s: expected true, got ", source);
    ot_write(rt, stderr, result);
    fputc('\n', stderr);
    abort();
  }
}

static void fails(ot_runtime* rt, const char* source, const char* message) {
  ot_value result;
  assert(!ot_eval(rt, source, strlen(source), &result));
  assert(strstr(ot_error(rt), message));
}

int main(void) {
  ot_io_host host = {host_open, host_read, host_write, host_close, host_flush};
  ot_runtime* rt = ot_create(2 * 1024 * 1024, 128 * 1024);
  assert(rt);
  Host h = {.rt = rt, .input = "a\xc3\xa9\xf0\x9f\x99\x82", .input_length = 7};
  assert(ot_set_io_host(rt, &host, &h));
  assert(ot_load_core(rt, "../lib"));
  ot_gc_stress(rt, true);
  ot_set_interrupt_flag(rt, &h.flag);
  check(rt, "(try (open-input-file \"missing\") (catch e (and (eq? (get e :reason) :not-found) "
            "(equal? (get e :path) \"missing\"))))");
  eval(rt, "(def p (open-input-file \"input\" {:buffer-size 4}))");
  h.read_error = EINTR;
  check(rt, "(and (eq? (read-char p) #\\a) (eq? (peek-char p) #\\é) (eq? (read-char p) #\\é) "
            "(eq? (read-char p) #\\🙂) (eof-object? (read-char p)))");
  eval(rt, "(close-port p) (close-port p)");
  assert(h.closes == 1 && h.reads >= 9);
  h.output_length = 0;
  eval(rt, "(def out (open-output-file \"output\" {:buffer-size 8 :if-exists :append})) "
           "(write-string \"abcdef\" out)");
  assert(!h.output_length && (h.flags & OT_IO_APPEND));
  h.write_error = ENOSPC;
  h.fail_after = 2;
  check(rt, "(try (flush-output-port out) (catch e (eq? (get e :reason) :no-space)))");
  assert(h.output_length == 2);
  eval(rt, "(flush-output-port out)");
  assert(h.output_length == 6 && !memcmp(h.output, "abcdef", 6));
  h.flush_error = EIO;
  check(rt, "(try (flush-output-port out) (catch e (eq? (get e :op) 'flush-output-port)))");
  eval(rt, "(flush-output-port out)");
  assert(h.output_length == 6);
  h.close_error = EIO;
  check(rt, "(try (close-port out) (catch e (eq? (get e :op) 'close-port)))");
  size_t closed = h.closes;
  eval(rt, "(close-port out)");
  assert(h.closes == closed);
  eval(rt, "(def interrupted (open-input-file \"input\"))");
  h.read_error = EINTR;
  h.interrupt = true;
  fails(rt, "(read-char interrupted)", "interrupted");
  assert(ot_interrupted(rt));
  h.interrupt = false;
  check(rt, "(eq? (read-char interrupted) #\\a)");
  check(rt, "(= (peek-u8 interrupted) 195)");
  h.read_error = EINTR;
  h.interrupt = true;
  fails(rt, "(read-char interrupted)", "interrupted");
  h.interrupt = false;
  check(rt, "(eq? (read-char interrupted) #\\é)");
  h.read_error = EACCES;
  check(rt, "(try (read-char interrupted) (catch e (eq? (get e :reason) :permission)))");
  check(rt, "(eq? (read-char interrupted) #\\🙂)");
  eval(rt, "(close-port interrupted)");
  eval(rt, "(def bad (open-input-file \"input\"))");
  h.invalid_read = true;
  fails(rt, "(read-u8 bad)", "invalid host read count");
  h.invalid_read = false;
  h.reenter = true;
  fails(rt, "(read-u8 bad)", "cannot reenter Otium");
  h.reenter = false;
  eval(rt, "(close-port bad)");
  h.reenter = true;
  closed = h.closes;
  fails(rt, "(open-output-file \"reentry\")", "cannot reenter Otium");
  h.reenter = false;
  assert(h.closes == closed + 1);
  eval(rt, "(def retry (open-output-file \"retry\" {:buffer-size 4})) (write-string \"ab\" retry)");
  h.invalid_write = true;
  fails(rt, "(flush-output-port retry)", "invalid host write count");
  h.invalid_write = false;
  size_t before = h.output_length;
  eval(rt, "(close-port retry)");
  assert(h.output_length == before + 2);
  eval(rt, "(def old (open-output-file \"old\")) (write-string \"old\" old)");
  Host replacement = {.rt = rt};
  assert(ot_set_io_host(rt, &host, &replacement));
  eval(rt, "(def new (open-output-file \"new\")) (write-string \"new\" new)");
  assert(ot_flush_ports(rt));
  assert(!memcmp(h.output + h.output_length - 3, "old", 3));
  assert(replacement.output_length == 3 && !memcmp(replacement.output, "new", 3));
  eval(rt, "(write-string \"end\" new)");
  size_t opens = h.opens;
  assert(ot_collections(rt) > 100);
  ot_destroy(rt);
  assert(h.closes == opens);
  assert(replacement.closes == replacement.opens);
  assert(replacement.output_length == 6 && !memcmp(replacement.output, "newend", 6));
  puts("embedding I/O host, managed buffers, partial transfers and reentry checks passed");
}
