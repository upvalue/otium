#include "heap.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static ot_runtime* create(size_t heap, size_t stack) {
  ot_runtime* rt = ot_create(heap, stack);
  return rt;
}

static ot_value evaluate(ot_runtime* rt, const char* source) {
  ot_value v;
  if (!ot_eval(rt, source, strlen(source), &v)) {
    fprintf(stderr, "%s\n%zu: %s\n", source, ot_error_offset(rt), ot_error(rt));
    abort();
  }
  return v;
}

static void integer(ot_runtime* rt, const char* source, intptr_t expected) {
  ot_value v = evaluate(rt, source);
  if (!ot_is_int(v) || ot_to_int(v) != expected) {
    fprintf(stderr, "%s\nexpected integer %ld, got ", source, (long)expected);
    ot_print(rt, stderr, v);
    fputc('\n', stderr);
    abort();
  }
}

static void rejects(ot_runtime* rt, const char* source, const char* message) {
  ot_value v;
  if (ot_eval(rt, source, strlen(source), &v) || !strstr(ot_error(rt), message)) {
    fprintf(stderr, "expected error containing %s for %s, got %s\n", message, source,
            ot_error(rt));
    abort();
  }
  integer(rt, "(+ 20 22)", 42);
}

static void core(void) {
  ot_runtime* rt = create(128 * 1024, 32768);
  assert(rt);
  integer(rt, "(+ 1 (* 2 3) (- 8 2))", 13);
  integer(rt, "(def (nary a b c) (+ a b c)) (nary 10 20 12)", 42);
  integer(rt, "(- 100 20 30 8)", 42);
  integer(rt, "(* 2 3 7)", 42);
  integer(rt, "(+ 1 2 3 4 5 6 7 8)", 36);
  integer(rt, "(+ 1 2 3 4 5 6 7 8 9)", 45);
  assert(evaluate(rt, "(= 42 (+ 10 20 12) (* 2 3 7) (- 100 20 30 8))") == OT_TRUE);
  assert(evaluate(rt, "(< 1 2 3 4 5 6 7 8)") == OT_TRUE);
  assert(evaluate(rt, "(< 1 2 3 4 4 6 7 8)") == OT_FALSE);
  assert(evaluate(rt, "(>= 8 7 7 1)") == OT_TRUE);
  assert(evaluate(rt, "(<= 1 1 2 3)") == OT_TRUE);
  assert(evaluate(rt, "(> 3 2 1)") == OT_TRUE);
  assert(evaluate(rt, "(= (+ 1 2.5 3) 6.5)") == OT_TRUE);
  assert(evaluate(rt, "(< 1 1.5 2)") == OT_TRUE);
  assert(evaluate(rt, "(= 9007199254740993 9007199254740992.0 9007199254740993)") == OT_FALSE);
  integer(rt, "(+ 4611686018427387903 1 -1)", 4611686018427387903);
  integer(rt, "(* 4611686018427387903 2 0)", 0);
  integer(rt, "(- -4611686018427387904 1 -1)", -4611686018427387904);
  assert(evaluate(rt, "(= (+ 4611686018427387903 1 0.0) 4611686018427387904.0)") == OT_TRUE);
  rejects(rt, "(< 2 1 #f)", "number");
  integer(rt,
          "(let ((saved +) (change (fn () (set! + (fn args 99)) 2))) (let ((answer (+ 1 (change) "
          "3))) (set! + saved) answer))",
          6);
  integer(
      rt,
      "(let ((saved +)) (set! + (fn args 42)) (let ((answer (+ 1 2 3))) (set! + saved) answer))",
      42);
  integer(rt, "(let ((x 10) (y (+ x 2)) (x (* x 2))) (+ x y))", 32);
  integer(rt, "(if nil 10 20)", 20);
  integer(rt, "(if #f 10 20)", 20);
  integer(rt, "(if '() 10 20)", 10);
  integer(rt, "(if 0 10 20)", 10);
  assert(evaluate(rt, "(if #f 10)") == OT_NIL);
  integer(rt, "(let ((x 0)) (and #f (set! x 1)) (or 2 (set! x 3)) x)", 0);
  integer(rt, "(and 1 2 3)", 3);
  integer(rt, "(or nil #f 7)", 7);
  assert(evaluate(rt, "(and)") == OT_TRUE);
  assert(evaluate(rt, "(or)") == OT_FALSE);
  integer(rt, "(let ((x 0)) (while (< x 100) (set! x (+ x 1))) x)", 100);
  integer(rt, "(def (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))) (fib 20)", 6765);
  integer(rt, "(let ((x 0) (inc (fn () (set! x (+ x 1))))) (inc) (inc) x)", 2);
  integer(rt, "(def (counter x) (fn () (set! x (+ x 1)) x)) (def tick (counter 40)) (tick) (tick)",
          42);
  assert(evaluate(rt, "(let ((x 0)) (set! x 42))") == OT_NIL);
  integer(rt, "(car (set-car! (cons 1 2) 3))", 3);
  integer(rt, "(let ((x 17) (outer (fn () (fn () x)))) ((outer)))", 17);
  integer(rt, "(let ((x 10) (f (fn () x))) (let ((x 20)) (f)))", 10);
  integer(rt, "(let ((x 0)) (def (a) (b)) (def (b) (set! x (+ x 1))) (a) x)", 1);
  integer(rt,
          "(def (live) 1) (def saved live) (def (caller) (live)) (def (live) 2) (+ (* 10 (saved)) "
          "(caller))",
          12);
  integer(rt, "(def old-add +) (def + (fn (a b) (old-add a b 1))) (+ 20 21)", 42);
  evaluate(rt, "(def + old-add)");
  integer(rt, "(let ((outside 9)) (let loop ((x (let ((inside 2)) inside)) (y outside)) (+ x y)))",
          11);
  integer(rt, "(let ((x 9)) (let loop ((x 1) (y x)) (+ x y)))", 10);
  integer(rt, "(car (cdr '(10 20 . 30)))", 20);
  integer(rt, "(cdr (cdr '(10 20 . 30)))", 30);
  assert(evaluate(rt, "(eq? 'same 'same)") == OT_TRUE);
  assert(evaluate(rt, "(eq? :same :same)") == OT_TRUE);
  assert(evaluate(rt, "(eq? 'same :same)") == OT_FALSE);
  assert(evaluate(rt, "(= (/ 3 2) 1.5)") == OT_TRUE);
  integer(rt, "(/ 6 3)", 2);
  integer(rt, "(modulo -7 3)", 2);
  integer(rt, "(remainder -7 3)", -1);
  assert(evaluate(rt, "(= (+ 1 2.5) 3.5)") == OT_TRUE);
  assert(evaluate(rt, "(> 9007199254740993 9007199254740992.0)") == OT_TRUE);
  assert(evaluate(rt, "(< 4611686018427387903 4611686018427387904.0)") == OT_TRUE);
  assert(evaluate(rt, "(= -4611686018427387904 -4611686018427387904.0)") == OT_TRUE);
  assert(evaluate(rt, "(< -1.5 -1)") == OT_TRUE);
  rejects(rt, "(car 1)", "pair");
  rejects(rt, "(1 2)", "callable");
  rejects(rt, "(set! missing 1)", "unbound");
  rejects(rt, "missing", "unbound");
  rejects(rt, "(fn (x x) x)", "duplicate");
  rejects(rt, "(let ((x 1 2)) x)", "initializer");
  rejects(rt, "((fn (x) x) 1 2)", "arguments");
  rejects(rt, "(+ 1 #f)", "number");
  rejects(rt, "(/ 1 0)", "zero");
  integer(rt, "(+ 4611686018427387903 1)", -4611686018427387904);
  integer(rt, "(* 4611686018427387903 4)", -4);
  rejects(rt, "4611686018427387904", "immediate range");
  rejects(rt, "(let () (def x y) (def y 2) x)", "uninitialized");
  rejects(rt, "(let () (if #f (begin (def x 1))) ((fn () x)))", "definition is only allowed");
  rejects(rt, "(+ 1 (def invalid 2))", "definition is only allowed");
  rejects(rt, "(def before-compile-error 91) (fn (x x) x)", "duplicate");
  integer(rt, "before-compile-error", 91);
  rejects(rt, "(def before-reader-error 92) (", "unterminated");
  integer(rt, "before-reader-error", 92);
  integer(rt,
          "(def previous-add +) (def (change-add) (set! + (fn (a b) 100)) 2)"
          "(def (snapshot-call) (+ 1 (change-add))) (snapshot-call)",
          3);
  integer(rt, "(snapshot-call)", 100);
  evaluate(rt, "(def + previous-add)");
  ot_value args[] = {ot_int(20), ot_int(22)}, result;
  assert(ot_call(rt, "+", args, 2, &result) && result == ot_int(42));
  assert(ot_call(rt, "caller", NULL, 0, &result) && result == ot_int(2));
  assert(!ot_call(rt, "missing-global-call", NULL, 0, &result));
  assert(ot_call(rt, "+", args, 2, &result) && result == ot_int(42));
  ot_destroy(rt);
}

static void tail_calls(void) {
  ot_runtime* rt = create(8192, 256);
  assert(rt);
  integer(rt, "(let loop ((i 1000000) (acc 0)) (if (= i 0) acc (loop (- i 1) (+ acc 1))))",
          1000000);
  assert(ot_get_stats(rt).max_frames <= 2);
  integer(rt,
          "(def (even n) (if (= n 0) 1 (odd (- n 1))))"
          "(def (odd n) (if (= n 0) 0 (even (- n 1)))) (even 1000000)",
          1);
  assert(ot_get_stats(rt).max_frames <= 2);
  integer(rt,
          "(def (a n) (if (= n 0) 7 (b (- n 1) 10 20)))"
          "(def (b n x y) (if (= n 0) (+ x y) (a (- n 1)))) (a 100001)",
          30);
  assert(ot_get_stats(rt).max_frames <= 2);
  rejects(rt, "(def (recurse n) (+ 1 (recurse (+ n 1)))) (recurse 0)", "stack limit");
  ot_destroy(rt);
}

static void collecting(void) {
  ot_runtime* rt = create(8192, 1024);
  assert(rt);
  integer(rt,
          "(def (maker x) (fn () (car x)))"
          "(let ((keep (maker (cons 42 nil))))"
          " (let loop ((i 100000) (p (cons 0 nil)))"
          "  (if (= i 0) (+ (keep) (car p))"
          "      (loop (- i 1) (cons 1 nil)))))",
          43);
  assert(ot_get_stats(rt).collections > 100);
  ot_gc_stress(rt, true);
  integer(rt,
          "(def (rooted-rec n p) (if (= n 0) (car p) (let ((keep (cons n p))) (+ (rooted-rec (- n "
          "1) (cons n nil)) (car keep))))) (rooted-rec 40 (cons 0 nil))",
          821);
  integer(rt,
          "(def (boxed-rec n) (let ((f (fn () n))) (if (= n 0) 0 (+ (boxed-rec (- n 1)) (f))))) "
          "(boxed-rec 40)",
          820);
  integer(rt,
          "(def (changed-rec n) (if (= n 0) 0 (+ 1 (changed-rec (begin (set! changed-rec (fn (x) "
          "100)) (- n 1)))))) (changed-rec 2)",
          102);
  integer(rt,
          "(let loop ((i 500) (f (maker (cons 42 nil))))"
          " (if (= i 0) (f) (loop (- i 1) (maker (cons 42 nil)))))",
          42);
  integer(rt,
          "(let ((p '(10 20 . 30)) (q (list '(a b) '(c d))))"
          " (let ((f (fn () (cons p q)))) (car (car (f)))))",
          10);
  assert(evaluate(rt, "(eq? 'retained 'retained)") == OT_TRUE);
  integer(rt, "(car (cdr (list (cons 1 2) 42 (cons 3 4))))", 42);
  integer(rt,
          "(let loop ((n 200) (f (fn () 0))) "
          "(if (= n 0) (f) (loop (- n 1) (fn () n))))",
          1);
  integer(rt,
          "(let loop ((n 200) (f (fn () 0))) (let ((x n)) "
          "(if (= n 0) (f) (loop (- n 1) (fn () x)))))",
          1);
  ot_value argument = evaluate(rt, "'(42)");
  ot_value result;
  assert(ot_call(rt, "maker", &argument, 1, &result));
  ot_gc_stress(rt, false);
  rejects(rt, "(let loop ((i 100000) (p '())) (if (= i 0) p (loop (- i 1) (cons i p))))",
          "heap exhausted");
  ot_destroy(rt);
}

static void large_frame(void) {
  ot_runtime* rt = create(128 * 1024, 2048);
  assert(rt);
  char source[16384];
  size_t used = (size_t)snprintf(source, sizeof(source), "(let (");
  for (unsigned i = 0; i < 300; i++) {
    int count = snprintf(source + used, sizeof(source) - used, "(v%u (cons %u nil))", i, i);
    assert(count > 0 && (size_t)count < sizeof(source) - used);
    used += (size_t)count;
  }
  assert(sizeof(source) - used > 32);
  strcpy(source + used, ") (car v299))");
  ot_gc_stress(rt, true);
  integer(rt, source, 299);
  ot_destroy(rt);
}

static void arrays(void) {
  ot_runtime* rt = create(32 * 1024, 4096);
  assert(rt);
  ot_gc_stress(rt, true);
  assert(evaluate(rt, "(equal? [1 [2] '(3 . 4)] [1 [2] '(3 . 4)])") == OT_TRUE);
  assert(evaluate(rt, "(equal? [1] [1.0])") == OT_FALSE);
  assert(evaluate(rt, "(equal? [1] '(1))") == OT_FALSE);
  assert(evaluate(rt, "(contains? [[1]] [1])") == OT_TRUE);
  assert(
      evaluate(rt, "(let ((a [1])) (push! a 2 3 4) (pop! a) (pop! a) (pop! a) (equal? a [1]))") ==
      OT_TRUE);
  assert(evaluate(rt, "(let ((a []) (b [])) (push! a a) (push! b b) (equal? a b))") == OT_TRUE);
  assert(evaluate(rt, "(let ((a []) (b [])) (push! a a 1) (push! b b 2) (equal? a b))") == OT_FALSE);
  assert(
      evaluate(
          rt,
          "(let ((a (cons 1 nil)) (b (cons 1 nil))) (set-cdr! a a) (set-cdr! b b) (equal? a b))") ==
      OT_TRUE);
  assert(evaluate(rt, "(get [nil] 0 9)") == OT_NIL);
  integer(rt, "([1 2] 9 42)", 42);
  integer(rt, "(let ((a [[1]]) (b (copy a))) (put! (get b 0) 0 42) ((a 0) 0))", 42);
  integer(rt,
          "(let ((a []) (alias a) (i 0)) (while (< i 500) (push! a (cons i nil)) (set! i (+ i 1))) "
          "(while (> (length a) 1) (pop! a)) (push! a (cons 42 nil)) (car (alias 1)))",
          42);
  integer(rt, "(def callable [42]) (callable 0)", 42);
  integer(rt,
          "(let ((saved get) (replace (fn () (set! get (fn (a b) 99)) 0))) (def (indexed a) (get a "
          "(replace))) (let ((answer (indexed [42]))) (set! get saved) answer))",
          42);
  integer(rt,
          "(let ((saved push!)) (set! push! (fn (a b) 42)) (let ((answer (push! [1] 2))) (set! "
          "push! saved) answer))",
          42);

  ot_value index = ot_int(0), result;
  assert(ot_call(rt, "callable", &index, 1, &result) && ot_to_int(result) == 42);
  rejects(rt, "(put! [] 0 1)", "index");
  rejects(rt, "(push! 1 2)", "array");
  rejects(rt, "([1])", "index");
  rejects(rt, "(get [1])", "arguments");
  ot_destroy(rt);
  rt = create(8192, 4096);
  assert(rt);
  ot_gc_stress(rt, true);
  integer(rt,
          "(let ((a []) (i 0)) (while (< i 10000) (push! a (cons i nil)) (pop! a) (set! i (+ i "
          "1))) (length a))",
          0);
  integer(rt,
          "(let ((a []) (i 0)) (while (< i 20) (push! a (let loop ((n 150) (p '())) (if (= n 0) p "
          "(loop (- n 1) (cons n p))))) (pop! a) (set! i (+ i 1))) (length a))",
          0);
  ot_gc_stress(rt, false);
  rejects(rt, "(def growing [42]) (while #t (push! growing 1))", "heap exhausted");
  integer(rt, "(get growing 0)", 42);
  ot_destroy(rt);
  rt = create(4 * 1024 * 1024, 4096);
  assert(rt);
  assert(evaluate(rt, "(let ((a '()) (b '()) (i 0)) (while (< i 20000) (set! a (cons i a)) (set! b "
                      "(cons i b)) (set! i (+ i 1))) (equal? a b))") == OT_TRUE);
  ot_destroy(rt);
}

static void tables(void) {
  ot_runtime* rt = create(256 * 1024, 8192);
  assert(rt);
  ot_gc_stress(rt, true);
  assert(evaluate(rt, "(equal? {:a 1} {:a 1})") == OT_FALSE);
  assert(evaluate(rt, "(equal? (keys {:a 1 :b 2 :a 3}) [:a :b])") == OT_TRUE);
  assert(evaluate(rt, "(equal? (values {:a 1 :b 2 :a 3}) [3 2])") == OT_TRUE);
  assert(
      evaluate(rt,
               "(let ((t {:a 1 :b 2})) (put! t :a nil) (put! t :a 3) (equal? (keys t) [:b :a]))") ==
      OT_TRUE);
  integer(rt, "(:a {:a 42})", 42);
  integer(rt, "(:a {} 42)", 42);
  integer(rt, "(get {} :missing 42)", 42);
  integer(rt,
          "(let ((saved put!) (replace (fn () (set! put! (fn (a b c) 99)) 42)) (t {})) (put! t :a "
          "(replace)) (set! put! saved) (t :a))",
          42);
  integer(rt, "({nil 42} nil)", 42);
  assert(evaluate(rt, "(contains? {:a #f} :a)") == OT_TRUE);
  assert(evaluate(rt, "(get {1 42} 1.0)") == OT_NIL);
  integer(rt, "(get {0.0 42} -0.0)", 42);
  integer(rt, "(get {\"same\" 42} \"same\")", 42);
  assert(evaluate(rt,
                  "(let ((a [1]) (b [1]) (t {})) (put! t a :array) (put! t (list a) :pair) (equal? "
                  "(list (t a) (t b) (t (list a)) (t (list b))) (list :array nil :pair nil)))") ==
         OT_TRUE);
  integer(rt,
          "(let ((a [1]) (t {a 42}) (i 0)) (push! a 2 3 4 5 6) (while (< i 500) (put! t i (cons i "
          "nil)) (set! i (+ i 1))) (t a))",
          42);
  integer(rt,
          "(let ((t {}) (i 0) (sum 0)) (while (< i 1000) (put! t i (* i 2)) (set! i (+ i 1))) "
          "(set! i 0) (while (< i 1000) (set! sum (+ sum (t i))) (put! t i nil) (set! i (+ i 1))) "
          "(+ sum (length t)))",
          999000);
  integer(rt,
          "(let ((t {:a [1] :b 2}) (c (copy t))) (put! t :b nil) (put! (c :a) 0 40) (+ ((t :a) 0) "
          "(c :b)))",
          42);
  integer(rt, "(def table-call {:x 42}) (table-call :x)", 42);
  ot_value key = evaluate(rt, ":x"), result;
  assert(ot_call(rt, "table-call", &key, 1, &result) && ot_to_int(result) == 42);
  rejects(rt, "(let ((k (cons 1 2))) (get {} k) (set-car! k 9))", "frozen table key");
  rejects(rt, "(let ((k (cons 1 2))) (put! {} k nil) (set-cdr! k 9))", "frozen table key");
  rejects(rt, "(let ((k (list (cons 1 2))) (t {})) (put! t k 42) (set-car! (car k) 9))",
          "frozen table key");
  integer(rt, "(let ((k (cons 1 nil)) (t {})) (set-cdr! k k) (put! t k 42) (t k))", 42);
  assert(evaluate(
             rt,
             "(let ((t {}) (i 0)) (while (< i 8) (put! t i i) (set! i (+ i 1))) (set! i 0) (while "
             "(< i 7) (put! t i nil) (set! i (+ i 1))) (put! t 8 8) (equal? (keys t) [7 8]))") ==
         OT_TRUE);
  rejects(rt, "(table 1)", "pairs");
  rejects(rt, "(keys [])", "table");
  rejects(rt, "(put! {} 1)", "arguments");
  ot_destroy(rt);
  rt = create(8192, 4096);
  assert(rt);
  ot_gc_stress(rt, true);
  integer(rt,
          "(let ((t {}) (i 0)) (while (< i 1000) (put! t (cons i nil) (cons i nil)) (put! t (cons "
          "i nil) nil) (set! i (+ i 1))) (length t))",
          0);
  ot_gc_stress(rt, false);
  rejects(
      rt,
      "(def growing-table {:keep 42}) (let loop ((i 0)) (put! growing-table i i) (loop (+ i 1)))",
      "heap exhausted");
  integer(rt, "(growing-table :keep)", 42);
  ot_destroy(rt);
}

static void rest_arguments(void) {
  ot_runtime* rt = create(8192, 4096);
  assert(rt);
  ot_gc_stress(rt, true);
  assert(evaluate(rt, "(equal? ((fn args args) 1 2 3) '(1 2 3))") == OT_TRUE);
  assert(evaluate(rt, "(equal? ((fn (x . args) (cons x args)) 1 2 3) '(1 2 3))") == OT_TRUE);
  assert(evaluate(rt, "(equal? ((fn (x & args) (cons x args)) 1 2 3) '(1 2 3))") == OT_TRUE);
  assert(evaluate(rt, "(null? ((fn args args)))") == OT_TRUE);
  assert(evaluate(rt, "(null? ((fn (x & args) args) 1))") == OT_TRUE);
  assert(evaluate(rt, "(def (gather . args) args) (equal? (gather 1 2 3) '(1 2 3))") == OT_TRUE);
  assert(evaluate(rt, "(eq? (gather 1) (gather 1))") == OT_FALSE);
  integer(rt, "(let ((f ((fn args (fn () (car (car args)))) (cons 42 nil)))) (f))", 42);
  integer(rt,
          "(def (rest-loop n previous & args) (if (= n 0) (previous) (rest-loop (- n 1) (fn () "
          "args) n))) (car (rest-loop 500 (fn () nil) 99))",
          2);
  assert(ot_get_stats(rt).max_frames < 5);
  integer(rt,
          "(def (rest-rec n & args) (if (= n 0) (car (car args)) (+ (rest-rec (- n 1) (cons n "
          "nil)) (car (car args))))) (rest-rec 30 (cons 0 nil))",
          465);
  ot_value arguments[] = {ot_int(1), ot_int(2), ot_int(3)}, result;
  assert(ot_call(rt, "gather", arguments, 3, &result));
  assert(value_is_ptr(result));
  size_t collections = ot_get_stats(rt).collections;
  for (unsigned i = 0; i < 10000; i++)
    assert(ot_call(rt, "gather", NULL, 0, &result) && result == OT_EMPTY);
  assert(ot_get_stats(rt).collections == collections);
  ot_value many[512];
  for (size_t i = 0; i < 512; i++) many[i] = ot_int((intptr_t)i);
  assert(!ot_call(rt, "gather", many, 512, &result));
  assert(strstr(ot_error(rt), "heap exhausted"));
  integer(rt, "(+ 20 22)", 42);
  rejects(rt, "((fn (x & args) args))", "arguments");
  rejects(rt, "(fn (x & x) x)", "duplicate");
  rejects(rt, "(fn (x . x) x)", "duplicate");
  rejects(rt, "(fn (x &) x)", "rest");
  rejects(rt, "(fn (x & y z) x)", "rest");
  rejects(rt, "(fn (x & y . z) x)", "rest");
  rejects(rt, "(fn (x . 1) x)", "rest");
  ot_destroy(rt);
}

static void apply_calls(void) {
  ot_runtime* rt = create(32768, 4096);
  assert(rt);
  ot_gc_stress(rt, true);
  integer(rt, "(apply + '(1 2 3))", 6);
  integer(rt, "(apply + 1 2 [3 4])", 10);
  integer(rt, "(apply + nil)", 0);
  integer(rt, "(apply apply (list + '(20 22)))", 42);
  integer(rt, "(apply [42] '(0))", 42);
  integer(rt, "(apply :answer (list {:answer 42}))", 42);
  integer(rt, "(apply {:answer 42} [:answer])", 42);
  assert(evaluate(rt, "(equal? (apply list 1 2 '(3 4)) '(1 2 3 4))") == OT_TRUE);
  assert(evaluate(rt, "(equal? (apply (fn args args) 1 '(2 3)) '(1 2 3))") == OT_TRUE);
  integer(
      rt,
      "(let ((x (cons 42 nil))) (apply (fn (p) (let ((garbage (cons 0 nil))) (car p))) (list x)))",
      42);
  integer(rt,
          "(def saved-apply apply) (let ((change (fn () (set! apply (fn (a b) 99)) '(20 22)))) "
          "(saved-apply (fn (x) x) (list (apply + (change)))))",
          42);
  evaluate(rt, "(set! apply saved-apply)");
  integer(rt,
          "(def (apply-loop n total) (if (= n 0) total (apply apply-loop (- n 1) (list (+ total "
          "1))))) (apply-loop 10000 0)",
          10000);
  assert(ot_get_stats(rt).max_frames < 5);
  integer(rt,
          "(def (apply-rec n p) (if (= n 0) (car p) (+ (apply apply-rec (list (- n 1) (cons n "
          "nil))) (car p)))) (apply-rec 30 (cons 0 nil))",
          465);
  ot_value arguments[2];
  arguments[0] = evaluate(rt, "+");
  arguments[1] = evaluate(rt, "'(20 22)");
  ot_value answer;
  assert(ot_call(rt, "apply", arguments, 2, &answer) && answer == ot_int(42));
  rejects(rt, "(apply +)", "argument sequence");
  rejects(rt, "(apply list 1)", "proper list");
  rejects(rt, "(apply list '(1 . 2))", "proper list");
  rejects(rt, "(let ((p (cons 1 nil))) (set-cdr! p p) (apply list p))", "acyclic");
  rejects(rt, "(apply 1 '())", "callable");
  rejects(rt, "(apply (fn (x) x) '())", "arguments");
  ot_destroy(rt);
  rt = create(32768, 32);
  assert(rt);
  rejects(rt,
          "(let loop ((n 40) (xs '())) (if (= n 0) (apply list xs) (loop (- n 1) (cons n xs))))",
          "stack limit");
  ot_destroy(rt);
}

static void optional_arguments(void) {
  ot_runtime* rt = create(16384, 4096);
  assert(rt);
  ot_gc_stress(rt, true);
  integer(rt, "((fn (x &optional (y (+ x 1)) (z (+ y 1))) (+ x y z)) 1)", 6);
  integer(rt, "((fn (x &optional (y (+ x 1)) (z (+ y 1))) (+ x y z)) 1 20)", 42);
  integer(rt, "(let ((x 41)) ((fn (&optional (x (+ x 1))) x)))", 42);
  assert(evaluate(rt, "((fn (&optional (x 42)) x) nil)") == OT_NIL);
  assert(evaluate(rt, "((fn (&optional (x 42)) x) #f)") == OT_FALSE);
  integer(rt, "((fn (&optional (x (let ((a 20) (b 21)) (+ a b))) (y (+ x 1))) y))", 42);
  integer(rt, "((fn (&optional (x (cons 41 nil)) (y (fn () (car x)))) (set-car! x 42) (y)))", 42);
  integer(rt,
          "(def hits 0) (def (defaulted &optional (x (begin (set! hits (+ hits 1)) hits)) (y "
          "(begin (set! hits (+ hits 1)) hits))) (+ x y)) (defaulted) (defaulted 20 22) hits",
          2);
  integer(rt, "(apply defaulted '(20 22))", 42);
  integer(rt,
          "(def (optional-loop n &optional (x (cons n nil))) (if (= n 0) (car x) (optional-loop (- "
          "n 1)))) (optional-loop 10000)",
          0);
  assert(ot_get_stats(rt).max_frames < 5);
  integer(rt,
          "(def (optional-rec n &optional (p (cons n nil))) (if (= n 0) (car p) (+ (optional-rec "
          "(- n 1)) (car p)))) (optional-rec 30)",
          465);
  integer(rt,
          "(def (optional-keep n previous &optional (x (cons n nil))) (if (= n 0) (previous) "
          "(optional-keep (- n 1) (fn () (car x))))) (optional-keep 500 (fn () 99))",
          1);
  ot_value values[] = {ot_int(20), ot_int(22)}, result;
  assert(ot_call(rt, "defaulted", values, 2, &result) && result == ot_int(42));
  integer(rt, "(def (optional-fast x &optional (y 1)) (+ x y)) (optional-fast 41)", 42);
  size_t collections = ot_get_stats(rt).collections;
  ot_value argument = ot_int(41);
  for (unsigned i = 0; i < 10000; i++)
    assert(ot_call(rt, "optional-fast", &argument, 1, &result) && result == ot_int(42));
  assert(ot_get_stats(rt).collections == collections);
  rejects(rt, "(defaulted 1 2 3)", "arguments");
  integer(rt, "hits", 2);
  rejects(rt, "((fn (required &optional (x 1)) x))", "arguments");
  rejects(rt, "(fn (x &optional) x)", "optional");
  rejects(rt, "(fn (&optional x) x)", "default");
  rejects(rt, "(fn (&optional (x)) x)", "default");
  rejects(rt, "(fn (&optional (x 1 2)) x)", "default");
  rejects(rt, "(fn (&optional (x 1) & rest) x)", "default");
  rejects(rt, "(fn (&optional (x 1) . rest) x)", "rest");
  rejects(rt, "(fn (x &optional (x 1)) x)", "duplicate");
  rejects(rt, "(fn (&optional (x 1) (x 2)) x)", "duplicate");
  ot_destroy(rt);
}

int main(void) {
  optional_arguments();
  apply_calls();
  rest_arguments();
  tables();
  arrays();
  core();
  tail_calls();
  collecting();
  large_frame();
  puts("runtime tests passed");
}
