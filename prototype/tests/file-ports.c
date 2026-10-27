#define _POSIX_C_SOURCE 200809L
#include "helpers.h"
#include <stdlib.h>
#include <unistd.h>

static ot_state* file_runtime(void) {
  ot_state* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  return rt;
}

/* Each check works on the file left by the ones before it. */
TEST(file_port_modes_buffering_and_errors) {
  char directory[] = "/tmp/otium-file-XXXXXX";
  CHECK(mkdtemp(directory));
  char path[128];
  snprintf(path, sizeof(path), "%s/data", directory);
  ot_state* rt = file_runtime();
  const char* args[] = {path, directory};
  CHECK(ot_set_arguments(rt, NULL, 2, args));
  check_true(rt, "(def path (car (otium.process/arguments))) (def directory (car (cdr "
                 "(otium.process/arguments)))) #t");
  check_true(rt, "(try (open-input-file path) (catch e (and (condition-of-type? e 'file-error) "
                 "(eq? (get e :reason) :not-found) (equal? (get e :path) path))))");
  check_true(rt, "(with ((p (open-output-file path {:buffer-size 1}))) (write-string \"aé🙂\" p)) "
                 "(equal? (read-file path) \"aé🙂\")");
  check_true(rt, "(with ((p (open-binary-input-file path {:buffer-size 1}))) (and (eq? (read-char "
                 "p) #\\a) (eq? (peek-char p) #\\é) (= (read-u8 p) 195) (= (read-u8 p) 169) "
                 "(equal? (read-all p) \"🙂\")))");
  check_true(rt, "(try (open-output-file path {:if-exists :error}) (catch e (eq? (get e :reason) "
                 ":exists)))");
  check_true(rt, "(with ((p (open-binary-output-file path {:if-exists :append}))) (write-u8 33 p)) "
                 "(equal? (read-file path) \"aé🙂!\")");
  check_true(rt, "(write-file path \"new\") (equal? (read-file path) \"new\")");
  check_true(rt, "(and (try (open-input-file directory) (catch e (eq? (get e :reason) "
                 ":is-directory))) (try (open-output-file directory) (catch e (eq? (get e "
                 ":reason) :is-directory))))");
  check_true(rt, "(try (open-output-file path {:buffer-size 0}) (catch e #t))");
  check_true(rt, "(equal? (read-file path) \"new\")");
  check_true(rt, "(let ((p (open-input-file path))) (close-port p) (close-port p) (and (not "
                 "(contains? (open-ports) p)) (try (read-u8 p) (catch e (eq? (get e :reason) "
                 ":closed)))))");
  check_true(rt,
             "(def final-port (open-output-file path)) (write-string \"shutdown\" final-port) #t");
  CHECK(ot_flush_ports(rt));
  check_true(rt, "(equal? (read-file path) \"shutdown\")");
  ot_destroy(rt);
  CHECK(!unlink(path));
  CHECK(!rmdir(directory));
}

TEST(full_device_reports_no_space) {
  if (access("/dev/full", W_OK)) return;
  ot_state* rt = file_runtime();
  check_true(rt, "(def full-port (open-output-file \"/dev/full\")) (try (begin (write-string "
                 "\"failure\" full-port) (flush-output-port full-port) #f) (catch e (and "
                 "(condition-of-type? e 'file-error) (eq? (get e :reason) :no-space))))");
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(file_port_modes_buffering_and_errors),
    TEST_ENTRY(full_device_reports_no_space),
};

TEST_SUITE(file_ports, tests);
