#include "helpers.h"

static ot_state* records_runtime(void) {
  ot_state* rt = new_runtime(1024 * 1024, 128 * 1024);
  CHECK(ot_add_load_path(rt, "../tests/records/fixtures"));
  CHECK(ot_load_core(rt, TEST_LIBRARY_DIR));
  ot_gc_stress(rt, true);
  return rt;
}

TEST(record_checks_script) {
  ot_state* rt = records_runtime();
  eval_file(rt, "../tests/records/checks.ot");
  ot_destroy(rt);
}

/* Each check builds on the records defined by earlier ones. */
TEST(record_layouts_retention_and_immutability) {
  ot_state* rt = records_runtime();
  const char* checks[] = {
      "(def names (list 'a 'b)) (def copied-fields (%make-record-type 'Copied names #f)) (set-car! "
      "names 'changed) (def copied (record-new copied-fields 1 2)) (and (= (copied :a) 1) (= "
      "(copied :b) 2))",
      "(def loop-fields (list 'a)) (set-cdr! loop-fields loop-fields) (try (%make-record-type "
      "'Cycle loop-fields #f) (catch e #t))",
      "(try (%make-record-type 'Improper '(a . b) #f) (catch e #t))",
      "(try (%make-record-type 'Invalid '(a) :yes) (catch e #t))",
      "(defrecord Retained (item next)) (def first-record (record-new Retained [] nil)) (def "
      "record-index {first-record :found}) (let loop ((i 500) (previous first-record)) (if (= i 0) "
      "(begin (record-set! first-record :next previous) #t) (loop (- i 1) (record-new Retained [i] "
      "previous))))",
      "(push! (first-record :item) 42) (and (eq? (get record-index first-record) :found) (= (get "
      "(first-record :item) 0) 42) (record? (first-record :next)))",
      "(defrecord Frozen (item) :immutable #t) (def frozen (record-new Frozen [])) (push! (frozen "
      ":item) 1) (and (= (get (frozen :item) 0) 1) (try (set! (frozen :item) []) (catch e #t)))",
      "(try (copied :a :b) (catch e #t))",
      "(try (record-ref copied 'a) (catch e #t))",
      "(and (= (copied :a) 1) (eq? (get record-index first-record) :found))"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) check_true(rt, checks[i]);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(record_checks_script),
    TEST_ENTRY(record_layouts_retention_and_immutability),
};

TEST_MAIN(tests)
