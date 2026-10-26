#include "helpers.h"

static ot_runtime* reader_runtime(void) {
  ot_runtime* rt = new_runtime(4 * 1024 * 1024, 256 * 1024);
  CHECK(ot_add_load_path(rt, "../tests/reader/data"));
  CHECK(ot_load_core(rt, TEST_LIBRARY_DIR));
  ot_gc_stress(rt, true);
  return rt;
}

TEST(reader_token_script) {
  ot_runtime* rt = reader_runtime();
  eval_file(rt, "../tests/reader/tokens.ot");
  ot_destroy(rt);
}

TEST(tokens_survive_collection) {
  ot_runtime* rt = reader_runtime();
  check_true(rt, "(require otium.reader :as reader) (= (length (reader/tokens (string-repeat "
                 "\"λ ; comment\\r\\n\" 1000))) 2000)");
  check_true(rt, "(def saved-tokens (reader/tokens \"(a . b)\")) (reader/tokens \"[]\") "
                 "(equal? saved-tokens '((:open \"(\" 0 0 0) (:atom \"a\" 0 1 0) (:atom "
                 "\".\" 0 3 0) (:atom \"b\" 0 5 0) (:close \")\" 0 6 0)))");
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(reader_token_script),
    TEST_ENTRY(tokens_survive_collection),
};

TEST_MAIN(tests)
