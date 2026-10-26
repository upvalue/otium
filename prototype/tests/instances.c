#include "helpers.h"
#include <stdlib.h>
#include <string.h>

/* The class checks up to the generic dispatch section define the classes and
 * instances (such as Widget and b) that the migration checks below use. Each
 * check also builds on the ones before it. */
TEST(class_construction_and_migration) {
  ot_runtime* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  size_t length;
  char* source = read_file("../tests/classes/checks.ot", &length);
  char* end = strstr(source, "(defgeneric (paint widget))");
  CHECK(end);
  *end = 0;
  eval(rt, source);
  free(source);
  const char* checks[] = {
      "(defabstract Other) (def old-widget Widget) (defabstract Widget :is Other) (and (eq? "
      "old-widget Widget) (is-a? b Other) (is-a? 42 Object) (not (is-a? nil Number)))",
      "(defclass Moving (a :init 1)) (def m (make Moving)) (def saved-class Moving) (defclass "
      "Moving (a :init 2) (b :init [])) (and (eq? Moving saved-class) (= (m :a) 1) (array? (m "
      ":b)))",
      "(defmethod (update-instance (m Moving) old) (when (contains? old :a) (slot-set! m :required "
      "(get old :a)))) (defclass Moving required) (= (m :required) 1)",
      "(defclass Retry (a :init 1)) (def r (make Retry)) (def tries 0) (defmethod (update-instance "
      "(r Retry) old) (set! tries (+ tries 1)) (when (not (= (get old :a) 1)) (error "
      ":bad-rollback)) (slot-set! r :a 99) (when (= tries 1) (error :retry))) (defclass Retry (a "
      ":init 2) (b :init 3)) (try (r :b) (catch e #t))",
      "(and (= (r :b) 3) (= (r :a) 99) (= tries 2))",
      "(defclass InitRetry (a :init 7)) (def ir (make InitRetry)) (def init-tries 0) (defclass "
      "InitRetry (a :init 99) (b :init (begin (set! init-tries (+ init-tries 1)) (if (= init-tries "
      "1) (error :retry) 8)))) (try (ir :b) (catch e #t))",
      "(and (= (ir :b) 8) (= (ir :a) 7) (= init-tries 2))",
      "(defclass Required (a :init 7)) (def rq (make Required)) (defclass Required a b) (try (rq "
      ":a) (catch e #t))",
      "(defclass Required (a :init 3)) (= (rq :a) 7)",
      "(def old-moving Moving) (reset-class! Moving (x :init 4)) (and (not (eq? old-moving "
      "Moving)) (is-a? m old-moving) (not (is-a? m Moving)) (= ((make Moving) :x) 4))",
      "(defclass State (x :init 1)) (def st (make State :x 9)) (defclass State (x :init 2 :state "
      "#t)) (= (st :x) 2)",
      "(equal? (str (make Moving)) \"#<Moving>\")",
      "(defclass Unchanged (x :init 1)) (def unchanged (make Unchanged)) (def hook-count 0) "
      "(defmethod (update-instance (x Unchanged) old) (set! hook-count (+ hook-count 1))) "
      "(defclass Unchanged (x :init 2)) (and (= (unchanged :x) 1) (= ((make Unchanged) :x) 2) (= "
      "hook-count 0))",
      "(def held (make Moving)) (def identity-map {held 42}) (= (get identity-map held) 42)"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) check_true(rt, checks[i]);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(class_construction_and_migration),
};

TEST_MAIN(tests)
