#include "helpers.h"

static ot_state* properties_runtime(void) {
  ot_state* rt = new_runtime(2 * 1024 * 1024, 256 * 1024);
  CHECK(ot_load_core(rt, TEST_LIBRARY_DIR));
  ot_gc_stress(rt, true);
  return rt;
}

TEST(ui_checks_script) {
  ot_state* rt = properties_runtime();
  eval_file(rt, "tests/ui/checks.ot");
  ot_destroy(rt);
}

/* Each check builds on the classes and instances made by earlier ones. */
TEST(property_mounting_and_migration_rollback) {
  ot_state* rt = properties_runtime();
  const char* checks[] = {
      "(def prop-count 0) (def state-count 0) (defclass Lifecycle (p :init (begin (set! prop-count "
      "(+ prop-count 1)) [])) (s :state #t :init (begin (set! state-count (+ state-count 1)) []))) "
      "(def props (class-properties Lifecycle)) (and (= prop-count 1) (= state-count 0) (nil? "
      "(props :s)))",
      "(def one (class-mount props)) (def two (class-mount props)) (and (= prop-count 1) (= "
      "state-count 2) (eq? (one :p) (two :p)) (not (eq? (one :s) (two :s))))",
      "(def old-state (one :s)) (defclass Lifecycle required (s :state #t :init []) (added :state "
      "#t :init 42)) (def hook-observed nil) (defmethod (update-instance (obj Lifecycle) old) "
      "(set! hook-observed (obj :required)) (slot-set! obj :added 99)) (class-update-properties! "
      "one (class-properties Lifecycle :required :new)) (and (eq? hook-observed :new) (eq? (one "
      ":s) old-state) (= (one :added) 99))",
      "(try (class-mount props) (catch e #t))",
      "(defclass Rollback p (s :state #t :init 1)) (def rollback (class-mount (class-properties "
      "Rollback :p :old))) (defclass Rollback p (s :state #t :init 0) (extra :state #t :init 2)) "
      "(def fail-hook #t) (defmethod (update-instance (obj Rollback) old) (when (not (eq? (get old "
      ":p) :old)) (error :bad-rollback)) (slot-set! obj :s 9) (when fail-hook (error :failure))) "
      "(def next-props (class-properties Rollback :p :new)) (try (class-update-properties! "
      "rollback next-props) (catch e #t))",
      "(set! fail-hook #f) (class-update-properties! rollback next-props) (and (eq? (rollback :p) "
      ":new) (= (rollback :s) 9) (= (rollback :extra) 2))",
      "(try (class-update-properties! one next-props) (catch e #t))",
      "(and (eq? (one :required) :new) (eq? (rollback :p) :new))"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) check_true(rt, checks[i]);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(ui_checks_script),
    TEST_ENTRY(property_mounting_and_migration_rollback),
};

TEST_SUITE(properties, tests);
