#include "helpers.h"
#include "internal.h"
#include <errno.h>
#include <string.h>

/* A fake I/O host that delivers one input byte and accepts two output bytes
 * per call, and can be told to fail, interrupt or reenter the runtime. */
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

/* Buffers handed to the host live in the managed heap, and the runtime refuses
 * every reentrant call while the host is running. */
static void managed(Host* h, const void* bytes, size_t length) {
  uintptr_t start = (uintptr_t)h->rt->heap.space[h->rt->heap.active];
  CHECK((uintptr_t)bytes >= start && (uintptr_t)bytes + length <= start + h->rt->heap.used);
  if (h->reenter) {
    size_t collections = ot_get_stats(h->rt).collections;
    ot_value result;
    CHECK(!ot_eval(h->rt, "(+ 1 2)", 7, &result));
    CHECK(!ot_call(h->rt, "+", NULL, 0, &result));
    CHECK(!ot_set_heap_limit(h->rt, 16 * 1024 * 1024));
    CHECK(!ot_flush_ports(h->rt));
    CHECK(!ot_set_io_host(h->rt, NULL, NULL));
    ot_destroy(h->rt);
    CHECK(ot_get_stats(h->rt).collections == collections);
  }
}

static int host_open(void* userdata, const char* path, unsigned flags, intptr_t* handle) {
  Host* h = userdata;
  managed(h, path, strlen(path));
  if (!strcmp(path, "missing")) return ENOENT;
  h->flags = flags;
  *handle = (intptr_t)(3 + h->opens++);
  CHECK(*handle < 128);
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
  CHECK(length);
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
  CHECK(h->output_length + take < sizeof(h->output));
  memcpy(h->output + h->output_length, bytes, take);
  h->output_length += take;
  return (long)take;
}

static int host_close(void* userdata, intptr_t handle) {
  Host* h = userdata;
  CHECK(handle >= 3);
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

static const ot_io_host host = {host_open, host_read, host_write, host_close, host_flush};

/* Expect an error containing message, without the recovery check that
 * check_error adds, since some of these run with the host misbehaving. */
static void fails(ot_runtime* rt, const char* source, const char* message) {
  ot_value result;
  if (ot_eval(rt, source, strlen(source), &result))
    test_fail(__FILE__, __LINE__, "%s: expected an error", source);
  if (!strstr(ot_error(rt), message))
    test_fail(__FILE__, __LINE__, "%s: expected \"%s\", got \"%s\"", source, message,
              ot_error(rt));
}

static ot_runtime* host_runtime(Host* h) {
  ot_runtime* rt = new_runtime(2 * 1024 * 1024, 128 * 1024);
  *h = (Host){.rt = rt, .input = "a\xc3\xa9\xf0\x9f\x99\x82", .input_length = 7};
  CHECK(ot_set_io_host(rt, &host, h));
  CHECK(ot_load_core(rt, TEST_LIBRARY_DIR));
  ot_gc_stress(rt, true);
  ot_set_interrupt_flag(rt, &h->flag);
  return rt;
}

TEST(host_input_and_eintr) {
  Host h;
  ot_runtime* rt = host_runtime(&h);
  check_true(rt, "(try (open-input-file \"missing\") (catch e (and (eq? (get e :reason) "
                 ":not-found) (equal? (get e :path) \"missing\"))))");
  eval(rt, "(def p (open-input-file \"input\" {:buffer-size 4}))");
  h.read_error = EINTR;
  check_true(rt, "(and (eq? (read-char p) #\\a) (eq? (peek-char p) #\\é) (eq? (read-char p) #\\é) "
                 "(eq? (read-char p) #\\🙂) (eof-object? (read-char p)))");
  eval(rt, "(close-port p) (close-port p)");
  CHECK(h.closes == 1 && h.reads >= 9);
  ot_destroy(rt);
}

TEST(host_partial_writes_and_errors) {
  Host h;
  ot_runtime* rt = host_runtime(&h);
  h.output_length = 0;
  eval(rt, "(def out (open-output-file \"output\" {:buffer-size 8 :if-exists :append})) "
           "(write-string \"abcdef\" out)");
  CHECK(!h.output_length && (h.flags & OT_IO_APPEND));
  h.write_error = ENOSPC;
  h.fail_after = 2;
  check_true(rt, "(try (flush-output-port out) (catch e (eq? (get e :reason) :no-space)))");
  CHECK(h.output_length == 2);
  eval(rt, "(flush-output-port out)");
  CHECK(h.output_length == 6 && !memcmp(h.output, "abcdef", 6));
  h.flush_error = EIO;
  check_true(rt, "(try (flush-output-port out) (catch e (eq? (get e :op) 'flush-output-port)))");
  eval(rt, "(flush-output-port out)");
  CHECK(h.output_length == 6);
  h.close_error = EIO;
  check_true(rt, "(try (close-port out) (catch e (eq? (get e :op) 'close-port)))");
  size_t closed = h.closes;
  eval(rt, "(close-port out)");
  CHECK(h.closes == closed);
  ot_destroy(rt);
}

TEST(host_interrupted_reads) {
  Host h;
  ot_runtime* rt = host_runtime(&h);
  eval(rt, "(def interrupted (open-input-file \"input\"))");
  h.read_error = EINTR;
  h.interrupt = true;
  fails(rt, "(read-char interrupted)", "interrupted");
  CHECK(ot_interrupted(rt));
  h.interrupt = false;
  check_true(rt, "(eq? (read-char interrupted) #\\a)");
  check_true(rt, "(= (peek-u8 interrupted) 195)");
  h.read_error = EINTR;
  h.interrupt = true;
  fails(rt, "(read-char interrupted)", "interrupted");
  h.interrupt = false;
  check_true(rt, "(eq? (read-char interrupted) #\\é)");
  h.read_error = EACCES;
  check_true(rt, "(try (read-char interrupted) (catch e (eq? (get e :reason) :permission)))");
  check_true(rt, "(eq? (read-char interrupted) #\\🙂)");
  eval(rt, "(close-port interrupted)");
  ot_destroy(rt);
}

TEST(host_invalid_counts_and_reentry) {
  Host h;
  ot_runtime* rt = host_runtime(&h);
  eval(rt, "(def bad (open-input-file \"input\"))");
  h.invalid_read = true;
  fails(rt, "(read-u8 bad)", "invalid host read count");
  h.invalid_read = false;
  h.reenter = true;
  fails(rt, "(read-u8 bad)", "cannot reenter Otium");
  h.reenter = false;
  eval(rt, "(close-port bad)");

  /* A failed open closes the handle the host returned. */
  h.reenter = true;
  size_t closed = h.closes;
  fails(rt, "(open-output-file \"reentry\")", "cannot reenter Otium");
  h.reenter = false;
  CHECK(h.closes == closed + 1);

  /* Buffered output survives a rejected write and is written on close. */
  eval(rt, "(def retry (open-output-file \"retry\" {:buffer-size 4})) (write-string \"ab\" retry)");
  h.invalid_write = true;
  fails(rt, "(flush-output-port retry)", "invalid host write count");
  h.invalid_write = false;
  size_t before = h.output_length;
  eval(rt, "(close-port retry)");
  CHECK(h.output_length == before + 2);
  ot_destroy(rt);
}

/* Ports keep the host they were opened with, and destroying the runtime
 * flushes and closes every port. */
TEST(host_replacement_and_destroy) {
  Host h;
  ot_runtime* rt = host_runtime(&h);
  eval(rt, "(def old (open-output-file \"old\")) (write-string \"old\" old)");
  Host replacement = {.rt = rt};
  CHECK(ot_set_io_host(rt, &host, &replacement));
  eval(rt, "(def new (open-output-file \"new\")) (write-string \"new\" new)");
  CHECK(ot_flush_ports(rt));
  CHECK(h.output_length >= 3 && !memcmp(h.output + h.output_length - 3, "old", 3));
  CHECK(replacement.output_length == 3 && !memcmp(replacement.output, "new", 3));
  eval(rt, "(write-string \"end\" new)");
  size_t opens = h.opens;
  CHECK(ot_get_stats(rt).collections > 100);
  ot_destroy(rt);
  CHECK(h.closes == opens);
  CHECK(replacement.closes == replacement.opens);
  CHECK(replacement.output_length == 6 && !memcmp(replacement.output, "newend", 6));
}

static const test_case tests[] = {
    TEST_ENTRY(host_input_and_eintr),
    TEST_ENTRY(host_partial_writes_and_errors),
    TEST_ENTRY(host_interrupted_reads),
    TEST_ENTRY(host_invalid_counts_and_reentry),
    TEST_ENTRY(host_replacement_and_destroy),
};

TEST_MAIN(tests)
