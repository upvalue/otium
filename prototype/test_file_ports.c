#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void check(ot_runtime* rt, const char* source) {
  ot_value result;
  if (!ot_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s\n%s\n", source, ot_error(rt));
    abort();
  }
  assert(result == OT_TRUE);
}

int main(void) {
  char directory[] = "/tmp/otium-file-XXXXXX";
  assert(mkdtemp(directory));
  char path[128];
  snprintf(path, sizeof(path), "%s/data", directory);
  ot_runtime* rt = ot_create(1024 * 1024, 128 * 1024);
  assert(rt);
  assert(ot_load_core(rt, "../lib"));
  ot_gc_stress(rt, true);
  const char* args[] = {path, directory};
  assert(ot_set_arguments(rt, NULL, 2, args));
  check(rt, "(def path (car (otium.process/arguments))) (def directory (car (cdr "
            "(otium.process/arguments)))) #t");
  check(rt, "(try (open-input-file path) (catch e (and (condition-of-type? e 'file-error) (eq? "
            "(get e :reason) :not-found) (equal? (get e :path) path))))");
  check(rt, "(with ((p (open-output-file path {:buffer-size 1}))) (write-string \"aé🙂\" p)) "
            "(equal? (read-file path) \"aé🙂\")");
  check(rt, "(with ((p (open-binary-input-file path {:buffer-size 1}))) (and (eq? (read-char p) "
            "#\\a) (eq? (peek-char p) #\\é) (= (read-u8 p) 195) (= (read-u8 p) 169) (equal? "
            "(read-all p) \"🙂\")))");
  check(
      rt,
      "(try (open-output-file path {:if-exists :error}) (catch e (eq? (get e :reason) :exists)))");
  check(rt, "(with ((p (open-binary-output-file path {:if-exists :append}))) (write-u8 33 p)) "
            "(equal? (read-file path) \"aé🙂!\")");
  check(rt, "(write-file path \"new\") (equal? (read-file path) \"new\")");
  check(rt, "(and (try (open-input-file directory) (catch e (eq? (get e :reason) :is-directory))) "
            "(try (open-output-file directory) (catch e (eq? (get e :reason) :is-directory))))");
  check(rt, "(try (open-output-file path {:buffer-size 0}) (catch e #t))");
  check(rt, "(equal? (read-file path) \"new\")");
  check(rt, "(let ((p (open-input-file path))) (close-port p) (close-port p) (and (not (contains? "
            "(open-ports) p)) (try (read-u8 p) (catch e (eq? (get e :reason) :closed)))))");
  check(rt, "(def final-port (open-output-file path)) (write-string \"shutdown\" final-port) #t");
  assert(ot_flush_ports(rt));
  check(rt, "(equal? (read-file path) \"shutdown\")");
  if (!access("/dev/full", W_OK)) {
    check(rt, "(def full-port (open-output-file \"/dev/full\")) (try (begin (write-string "
              "\"failure\" full-port) (flush-output-port full-port) #f) (catch e (and "
              "(condition-of-type? e 'file-error) (eq? (get e :reason) :no-space))))");
  }
  ot_destroy(rt);
  assert(!unlink(path));
  assert(!rmdir(directory));
  puts("file port ownership, buffering, modes and errors passed");
}
