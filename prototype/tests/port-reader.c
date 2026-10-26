#include "helpers.h"

static ot_runtime* reader_runtime(void) {
  ot_runtime* rt = new_core_runtime();
  CHECK(ot_add_load_path(rt, "../tests/reader/data"));
  ot_gc_stress(rt, true);
  return rt;
}

/* The script loads the reader data that the negative check uses. */
TEST(port_reader_script) {
  ot_runtime* rt = reader_runtime();
  eval_file(rt, "../tests/library/port-reader.ot");
  check_true(rt, "(for-all (fn (text) (try (read (open-input-string text)) #f (catch e #t))) "
                 "(drop 3 datums/negative))");
  ot_destroy(rt);
}

TEST(streaming_datum_boundaries) {
  ot_runtime* rt = reader_runtime();
  const char* checks[] = {
      "(let ((p (open-input-string \"(a)tail\"))) (and (equal? (read p) '(a)) (eq? (peek-char p) "
      "#\\t) (eq? (read p) 'tail)))",
      "(let ((p (open-input-string \"\\\"s\\\"tail\"))) (and (equal? (read p) \"s\") (eq? (read p) "
      "'tail)))",
      "(let ((p (open-input-string \"#\\\\)tail\"))) (and (eq? (read p) #\\)) (eq? (read p) "
      "'tail)))",
      "(let ((p (open-input-string \"#u8(1)tail\"))) (and (equal? (read p) '(bytevector 1)) (eq? "
      "(read p) 'tail)))",
      "(let ((p (open-input-string \"' ; c\\n (a . b) :tail\"))) (and (equal? (read p) '(quote (a "
      ". b))) (eq? (read p) :tail)))",
      "(let ((p (open-input-string \"; only comment\"))) (eof-object? (read p)))",
      "(let ((p (open-input-string \"   \"))) (eof-object? (read p)))",
      "(let ((p (open-input-string \"[(]) :tail\"))) (try (read p) (catch e #t)))",
      "(let ((p (open-input-string \"'\"))) (try (read p) (catch e #t)))",
      "(let ((p (open-input-string \"\\\"unfinished\"))) (try (read p) (catch e #t)))",
      "(let ((p (open-input-bytevector #u8(40 65 255 41)))) (try (read p) (catch e (and (eq? "
      "(condition-type e) 'decode-error) (= (get e :position) 2) (= (read-u8 p) 255)))))",
      "(with-params ((current-input-port (open-input-string \"[1 2]\"))) (equal? (read) '(array 1 "
      "2)))",
      "(let ((p (open-input-string \"nil #f ()\"))) (and (nil? (read p)) (eq? (read p) #f) (null? "
      "(read p)) (eof-object? (read p))))",
      "(let ((text (string-append \"(\" (string-repeat \"a \" 5000) \") :tail\")) (p "
      "(open-input-string text))) (and (= (length (read p)) 5000) (eq? (read p) :tail)))"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++) check_true(rt, checks[i]);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(port_reader_script),
    TEST_ENTRY(streaming_datum_boundaries),
};

TEST_MAIN(tests)
