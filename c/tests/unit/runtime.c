#include "heap.h"
#include "helpers.h"
#include <string.h>

TEST(core_evaluation) {
  ot_state* rt = new_runtime(128 * 1024, 32768);
  check_int(rt, "(+ 1 (* 2 3) (- 8 2))", 13);
  check_int(rt, "(def (nary a b c) (+ a b c)) (nary 10 20 12)", 42);
  check_int(rt, "(- 100 20 30 8)", 42);
  check_int(rt, "(* 2 3 7)", 42);
  check_int(rt, "(+ 1 2 3 4 5 6 7 8)", 36);
  check_int(rt, "(+ 1 2 3 4 5 6 7 8 9)", 45);
  check_true(rt, "(= 42 (+ 10 20 12) (* 2 3 7) (- 100 20 30 8))");
  check_true(rt, "(< 1 2 3 4 5 6 7 8)");
  CHECK(eval(rt, "(< 1 2 3 4 4 6 7 8)") == OT_FALSE);
  check_true(rt, "(>= 8 7 7 1)");
  check_true(rt, "(<= 1 1 2 3)");
  check_true(rt, "(> 3 2 1)");
  check_true(rt, "(= (+ 1 2.5 3) 6.5)");
  check_true(rt, "(< 1 1.5 2)");
  CHECK(eval(rt, "(= 9007199254740993 9007199254740992.0 9007199254740993)") == OT_FALSE);
  check_int(rt, "(+ 4611686018427387903 1 -1)", 4611686018427387903);
  check_int(rt, "(* 4611686018427387903 2 0)", 0);
  check_int(rt, "(- -4611686018427387904 1 -1)", -4611686018427387904);
  check_true(rt, "(= (+ 4611686018427387903 1 0.0) 4611686018427387904.0)");
  check_error(rt, "(< 2 1 #f)", "number");
  check_int(rt,
            "(def add +) (let ((saved add) (change (fn () (set! add (fn args 99)) 2))) "
            "(let ((answer (add 1 (change) 3))) (set! add saved) answer))",
            6);
  check_int(
      rt,
      "(let ((saved add)) (set! add (fn args 42)) (let ((answer (add 1 2 3))) (set! add saved) "
      "answer))",
      42);
  check_int(rt, "(let ((x 10) (y (+ x 2)) (x (* x 2))) (+ x y))", 32);
  check_int(rt, "(if nil 10 20)", 20);
  check_int(rt, "(if #f 10 20)", 20);
  check_int(rt, "(if '() 10 20)", 10);
  check_int(rt, "(if 0 10 20)", 10);
  CHECK(eval(rt, "(if #f 10)") == OT_NIL);
  check_int(rt, "(let ((x 0)) (and #f (set! x 1)) (or 2 (set! x 3)) x)", 0);
  check_int(rt, "(and 1 2 3)", 3);
  check_int(rt, "(or nil #f 7)", 7);
  check_true(rt, "(and)");
  CHECK(eval(rt, "(or)") == OT_FALSE);
  check_int(rt, "(let ((x 0)) (while (< x 100) (set! x (+ x 1))) x)", 100);
  check_int(rt, "(def (fib n) (if (< n 2) n (+ (fib (- n 1)) (fib (- n 2))))) (fib 20)", 6765);
  check_int(rt, "(let ((x 0) (inc (fn () (set! x (+ x 1))))) (inc) (inc) x)", 2);
  check_int(
      rt, "(def (counter x) (fn () (set! x (+ x 1)) x)) (def tick (counter 40)) (tick) (tick)", 42);
  CHECK(eval(rt, "(let ((x 0)) (set! x 42))") == OT_NIL);
  check_int(rt, "(car (set-car! (cons 1 2) 3))", 3);
  check_int(rt, "(let ((x 17) (outer (fn () (fn () x)))) ((outer)))", 17);
  check_int(rt, "(let ((x 10) (f (fn () x))) (let ((x 20)) (f)))", 10);
  check_int(rt, "(let ((x 0)) (def (a) (b)) (def (b) (set! x (+ x 1))) (a) x)", 1);
  check_int(
      rt,
      "(def (live) 1) (def saved live) (def (caller) (live)) (def (live) 2) (+ (* 10 (saved)) "
      "(caller))",
      12);
  check_int(rt, "(def old-add add) (def add (fn (a b) (old-add a b 1))) (add 20 21)", 42);
  eval(rt, "(def add old-add)");
  check_int(
      rt, "(let ((outside 9)) (let loop ((x (let ((inside 2)) inside)) (y outside)) (+ x y)))", 11);
  check_int(rt, "(let ((x 9)) (let loop ((x 1) (y x)) (+ x y)))", 10);
  check_int(rt, "(car (cdr '(10 20 . 30)))", 20);
  check_int(rt, "(cdr (cdr '(10 20 . 30)))", 30);
  check_true(rt, "(eq? 'same 'same)");
  check_true(rt, "(eq? :same :same)");
  CHECK(eval(rt, "(eq? 'same :same)") == OT_FALSE);
  check_true(rt, "(= (/ 3 2) 1.5)");
  check_int(rt, "(/ 6 3)", 2);
  check_int(rt, "(modulo -7 3)", 2);
  check_int(rt, "(remainder -7 3)", -1);
  check_true(rt, "(= (+ 1 2.5) 3.5)");
  check_true(rt, "(> 9007199254740993 9007199254740992.0)");
  check_true(rt, "(< 4611686018427387903 4611686018427387904.0)");
  check_true(rt, "(= -4611686018427387904 -4611686018427387904.0)");
  check_true(rt, "(< -1.5 -1)");
  check_error(rt, "(car 1)", "pair");
  check_error(rt, "(1 2)", "callable");
  check_error(rt, "(set! missing 1)", "unbound");
  check_error(rt, "missing", "unbound");
  check_error(rt, "(fn (x x) x)", "duplicate");
  check_error(rt, "(let ((x 1 2)) x)", "initializer");
  check_error(rt, "((fn (x) x) 1 2)", "arguments");
  check_error(rt, "(+ 1 #f)", "number");
  check_error(rt, "(/ 1 0)", "zero");
  check_int(rt, "(+ 4611686018427387903 1)", -4611686018427387904);
  check_int(rt, "(* 4611686018427387903 4)", -4);
  check_error(rt, "4611686018427387904", "immediate range");
  check_error(rt, "(let () (def x y) (def y 2) x)", "uninitialized");
  check_error(rt, "(let () (if #f (begin (def x 1))) ((fn () x)))", "definition is only allowed");
  check_error(rt, "(+ 1 (def invalid 2))", "definition is only allowed");
  check_error(rt, "(def before-compile-error 91) (fn (x x) x)", "duplicate");
  check_int(rt, "before-compile-error", 91);
  check_error(rt, "(def before-reader-error 92) (", "unclosed form");
  check_int(rt, "before-reader-error", 92);
  /* Inlined builtins are immutable. Whether a global callee is read before or
   * after its arguments are evaluated is unspecified: the VM reads it before
   * and native code after, so either result is right. */
  check_error(rt, "(set! + (fn (a b) 100))", "cannot redefine builtin");
  check_int(rt, "(+ 20 22)", 42);
  ot_value first;
  const char* snapshot = "(def (snapshot-target a b) (+ a b)) "
                         "(def (change-target) (set! snapshot-target (fn (a b) 100)) 2)"
                         "(def (snapshot-call) (snapshot-target 1 (change-target))) (snapshot-call)";
  CHECK(ot_eval(rt, snapshot, strlen(snapshot), &first));
  CHECK(first == ot_int(3) || first == ot_int(100));
  check_int(rt, "(snapshot-call)", 100);
  ot_value args[] = {ot_int(20), ot_int(22)}, result;
  CHECK(ot_call(rt, "+", args, 2, &result) && result == ot_int(42));
  CHECK(ot_call(rt, "caller", NULL, 0, &result) && result == ot_int(2));
  CHECK(!ot_call(rt, "missing-global-call", NULL, 0, &result));
  CHECK(ot_call(rt, "+", args, 2, &result) && result == ot_int(42));
  ot_destroy(rt);
}

TEST(tail_calls) {
  ot_state* rt = new_runtime(8192, 256);
  check_int(rt, "(let loop ((i 1000000) (acc 0)) (if (= i 0) acc (loop (- i 1) (+ acc 1))))",
            1000000);
  CHECK(ot_get_stats(rt).max_frames <= 2);
  check_int(rt,
            "(def (even n) (if (= n 0) 1 (odd (- n 1))))"
            "(def (odd n) (if (= n 0) 0 (even (- n 1)))) (even 1000000)",
            1);
  CHECK(ot_get_stats(rt).max_frames <= 2);
  check_int(rt,
            "(def (a n) (if (= n 0) 7 (b (- n 1) 10 20)))"
            "(def (b n x y) (if (= n 0) (+ x y) (a (- n 1)))) (a 100001)",
            30);
  CHECK(ot_get_stats(rt).max_frames <= 2);
  check_error(rt, "(def (recurse n) (+ 1 (recurse (+ n 1)))) (recurse 0)", "stack limit");
  ot_destroy(rt);
}

TEST(collection_keeps_live_values) {
  ot_state* rt = new_runtime(8192, 1024);
  check_int(rt,
            "(def (maker x) (fn () (car x)))"
            "(let ((keep (maker (cons 42 nil))))"
            " (let loop ((i 100000) (p (cons 0 nil)))"
            "  (if (= i 0) (+ (keep) (car p))"
            "      (loop (- i 1) (cons 1 nil)))))",
            43);
  CHECK(ot_get_stats(rt).collections > 100);
  ot_gc_stress(rt, true);
  check_int(
      rt,
      "(def (rooted-rec n p) (if (= n 0) (car p) (let ((keep (cons n p))) (+ (rooted-rec (- n "
      "1) (cons n nil)) (car keep))))) (rooted-rec 40 (cons 0 nil))",
      821);
  check_int(rt,
            "(def (boxed-rec n) (let ((f (fn () n))) (if (= n 0) 0 (+ (boxed-rec (- n 1)) (f))))) "
            "(boxed-rec 40)",
            820);
  check_int(rt,
            "(def (changed-rec n) (if (= n 0) 0 (+ 1 (changed-rec (begin (set! changed-rec (fn (x) "
            "100)) (- n 1)))))) (changed-rec 2)",
            102);
  check_int(rt,
            "(let loop ((i 500) (f (maker (cons 42 nil))))"
            " (if (= i 0) (f) (loop (- i 1) (maker (cons 42 nil)))))",
            42);
  check_int(rt,
            "(let ((p '(10 20 . 30)) (q (list '(a b) '(c d))))"
            " (let ((f (fn () (cons p q)))) (car (car (f)))))",
            10);
  check_true(rt, "(eq? 'retained 'retained)");
  check_int(rt, "(car (cdr (list (cons 1 2) 42 (cons 3 4))))", 42);
  check_int(rt,
            "(let loop ((n 200) (f (fn () 0))) "
            "(if (= n 0) (f) (loop (- n 1) (fn () n))))",
            1);
  check_int(rt,
            "(let loop ((n 200) (f (fn () 0))) (let ((x n)) "
            "(if (= n 0) (f) (loop (- n 1) (fn () x)))))",
            1);
  ot_value argument = eval(rt, "'(42)");
  ot_value result;
  CHECK(ot_call(rt, "maker", &argument, 1, &result));
  ot_gc_stress(rt, false);
  check_error(rt, "(let loop ((i 100000) (p '())) (if (= i 0) p (loop (- i 1) (cons i p))))",
              "heap exhausted");
  ot_destroy(rt);
}

TEST(large_frame) {
  ot_state* rt = new_runtime(128 * 1024, 2048);
  char source[16384];
  size_t used = (size_t)snprintf(source, sizeof(source), "(let (");
  for (unsigned i = 0; i < 300; i++) {
    int count = snprintf(source + used, sizeof(source) - used, "(v%u (cons %u nil))", i, i);
    CHECK(count > 0 && (size_t)count < sizeof(source) - used);
    used += (size_t)count;
  }
  CHECK(sizeof(source) - used > 32);
  strcpy(source + used, ") (car v299))");
  ot_gc_stress(rt, true);
  check_int(rt, source, 299);
  ot_destroy(rt);
}

TEST(arrays) {
  ot_state* rt = new_runtime(32 * 1024, 4096);
  ot_gc_stress(rt, true);
  check_true(rt, "(equal? [1 [2] '(3 . 4)] [1 [2] '(3 . 4)])");
  CHECK(eval(rt, "(equal? [1] [1.0])") == OT_FALSE);
  CHECK(eval(rt, "(equal? [1] '(1))") == OT_FALSE);
  check_true(rt, "(let ((a [1])) (push! a 2 3 4) (pop! a) (pop! a) (pop! a) (equal? a [1]))");
  check_true(rt, "(let ((a []) (b [])) (push! a a) (push! b b) (equal? a b))");
  CHECK(eval(rt, "(let ((a []) (b [])) (push! a a 1) (push! b b 2) (equal? a b))") == OT_FALSE);
  CHECK(eval(rt, "(get [nil] 0 9)") == OT_NIL);
  check_int(rt, "([1 2] 9 42)", 42);
  check_int(rt, "(let ((a [[1]]) (b (copy a))) (put! (get b 0) 0 42) ((a 0) 0))", 42);
  check_int(
      rt,
      "(let ((a []) (alias a) (i 0)) (while (< i 500) (push! a (cons i nil)) (set! i (+ i 1))) "
      "(while (> (length a) 1) (pop! a)) (push! a (cons 42 nil)) (car (alias 1)))",
      42);
  check_int(rt, "(def callable [42]) (callable 0)", 42);
  check_int(
      rt,
      "(let ((saved get) (replace (fn () (set! get (fn (a b) 99)) 0))) (def (indexed a) (get a "
      "(replace))) (let ((answer (indexed [42]))) (set! get saved) answer))",
      42);
  check_int(rt,
            "(let ((saved push!)) (set! push! (fn (a b) 42)) (let ((answer (push! [1] 2))) (set! "
            "push! saved) answer))",
            42);

  ot_value index = ot_int(0), result;
  CHECK(ot_call(rt, "callable", &index, 1, &result) && ot_to_int(result) == 42);
  check_error(rt, "(put! [] 0 1)", "index");
  check_error(rt, "(push! 1 2)", "array");
  check_error(rt, "([1])", "index");
  check_error(rt, "(get [1])", "arguments");
  ot_destroy(rt);
}

TEST(array_garbage_and_exhaustion) {
  ot_state* rt = new_runtime(8192, 4096);
  ot_gc_stress(rt, true);
  check_int(rt,
            "(let ((a []) (i 0)) (while (< i 10000) (push! a (cons i nil)) (pop! a) (set! i (+ i "
            "1))) (length a))",
            0);
  check_int(
      rt,
      "(let ((a []) (i 0)) (while (< i 20) (push! a (let loop ((n 150) (p '())) (if (= n 0) p "
      "(loop (- n 1) (cons n p))))) (pop! a) (set! i (+ i 1))) (length a))",
      0);
  ot_gc_stress(rt, false);
  check_error(rt, "(def growing [42]) (while #t (push! growing 1))", "heap exhausted");
  check_int(rt, "(get growing 0)", 42);
  ot_destroy(rt);
}

TEST(long_list_equality) {
  ot_state* rt = new_runtime(4 * 1024 * 1024, 4096);
  check_true(rt, "(let ((a '()) (b '()) (i 0)) (while (< i 20000) (set! a (cons i a)) (set! b "
                 "(cons i b)) (set! i (+ i 1))) (equal? a b))");
  ot_destroy(rt);
}

TEST(tables) {
  ot_state* rt = new_runtime(256 * 1024, 8192);
  ot_gc_stress(rt, true);
  CHECK(eval(rt, "(equal? {:a 1} {:a 1})") == OT_FALSE);
  check_true(rt, "(equal? (keys {:a 1 :b 2 :a 3}) [:a :b])");
  check_true(rt, "(equal? (values {:a 1 :b 2 :a 3}) [3 2])");
  check_true(rt, "(let ((t {:a 1 :b 2})) (put! t :a nil) (put! t :a 3) (equal? (keys t) [:b :a]))");
  check_int(rt, "(:a {:a 42})", 42);
  check_int(rt, "(:a {} 42)", 42);
  check_int(rt, "(get {} :missing 42)", 42);
  check_int(
      rt,
      "(let ((saved put!) (replace (fn () (set! put! (fn (a b c) 99)) 42)) (t {})) (put! t :a "
      "(replace)) (set! put! saved) (t :a))",
      42);
  check_int(rt, "({nil 42} nil)", 42);
  CHECK(eval(rt, "(get {1 42} 1.0)") == OT_NIL);
  check_int(rt, "(get {0.0 42} -0.0)", 42);
  check_int(rt, "(get {\"same\" 42} \"same\")", 42);
  check_true(rt, "(let ((a [1]) (b [1]) (t {})) (put! t a :array) (put! t (list a) :pair) (equal? "
                 "(list (t a) (t b) (t (list a)) (t (list b))) (list :array nil :pair nil)))");
  check_int(
      rt,
      "(let ((a [1]) (t {a 42}) (i 0)) (push! a 2 3 4 5 6) (while (< i 500) (put! t i (cons i "
      "nil)) (set! i (+ i 1))) (t a))",
      42);
  check_int(
      rt,
      "(let ((t {}) (i 0) (sum 0)) (while (< i 1000) (put! t i (* i 2)) (set! i (+ i 1))) "
      "(set! i 0) (while (< i 1000) (set! sum (+ sum (t i))) (put! t i nil) (set! i (+ i 1))) "
      "(+ sum (length t)))",
      999000);
  check_int(
      rt,
      "(let ((t {:a [1] :b 2}) (c (copy t))) (put! t :b nil) (put! (c :a) 0 40) (+ ((t :a) 0) "
      "(c :b)))",
      42);
  check_int(rt, "(def table-call {:x 42}) (table-call :x)", 42);
  ot_value key = eval(rt, ":x"), result;
  CHECK(ot_call(rt, "table-call", &key, 1, &result) && ot_to_int(result) == 42);
  check_error(rt, "(let ((k (cons 1 2))) (get {} k) (set-car! k 9))", "frozen table key");
  check_error(rt, "(let ((k (cons 1 2))) (put! {} k nil) (set-cdr! k 9))", "frozen table key");
  check_error(rt, "(let ((k (list (cons 1 2))) (t {})) (put! t k 42) (set-car! (car k) 9))",
              "frozen table key");
  check_true(rt,
             "(let ((t {}) (i 0)) (while (< i 8) (put! t i i) (set! i (+ i 1))) (set! i 0) (while "
             "(< i 7) (put! t i nil) (set! i (+ i 1))) (put! t 8 8) (equal? (keys t) [7 8]))");
  check_error(rt, "(table 1)", "pairs");
  check_error(rt, "(keys [])", "table");
  check_error(rt, "(put! {} 1)", "arguments");
  ot_destroy(rt);
}

TEST(table_garbage_and_exhaustion) {
  ot_state* rt = new_runtime(8192, 4096);
  ot_gc_stress(rt, true);
  check_int(
      rt,
      "(let ((t {}) (i 0)) (while (< i 1000) (put! t (cons i nil) (cons i nil)) (put! t (cons "
      "i nil) nil) (set! i (+ i 1))) (length t))",
      0);
  ot_gc_stress(rt, false);
  check_error(
      rt,
      "(def growing-table {:keep 42}) (let loop ((i 0)) (put! growing-table i i) (loop (+ i 1)))",
      "heap exhausted");
  check_int(rt, "(growing-table :keep)", 42);
  ot_destroy(rt);
}

TEST(rest_arguments) {
  ot_state* rt = new_runtime(8192, 4096);
  ot_gc_stress(rt, true);
  check_true(rt, "(equal? ((fn args args) 1 2 3) '(1 2 3))");
  check_true(rt, "(equal? ((fn (x . args) (cons x args)) 1 2 3) '(1 2 3))");
  check_true(rt, "(equal? ((fn (x & args) (cons x args)) 1 2 3) '(1 2 3))");
  check_true(rt, "(null? ((fn args args)))");
  check_true(rt, "(null? ((fn (x & args) args) 1))");
  check_true(rt, "(def (gather . args) args) (equal? (gather 1 2 3) '(1 2 3))");
  CHECK(eval(rt, "(eq? (gather 1) (gather 1))") == OT_FALSE);
  check_int(rt, "(let ((f ((fn args (fn () (car (car args)))) (cons 42 nil)))) (f))", 42);
  check_int(rt,
            "(def (rest-loop n previous & args) (if (= n 0) (previous) (rest-loop (- n 1) (fn () "
            "args) n))) (car (rest-loop 500 (fn () nil) 99))",
            2);
  CHECK(ot_get_stats(rt).max_frames < 5);
  check_int(rt,
            "(def (rest-rec n & args) (if (= n 0) (car (car args)) (+ (rest-rec (- n 1) (cons n "
            "nil)) (car (car args))))) (rest-rec 30 (cons 0 nil))",
            465);
  ot_value arguments[] = {ot_int(1), ot_int(2), ot_int(3)}, result;
  CHECK(ot_call(rt, "gather", arguments, 3, &result));
  CHECK(value_is_ptr(result));
  size_t collections = ot_get_stats(rt).collections;
  for (unsigned i = 0; i < 10000; i++)
    CHECK(ot_call(rt, "gather", NULL, 0, &result) && result == OT_EMPTY);
  CHECK(ot_get_stats(rt).collections == collections);
  ot_value many[512];
  for (size_t i = 0; i < 512; i++) many[i] = ot_int((intptr_t)i);
  CHECK(!ot_call(rt, "gather", many, 512, &result));
  CHECK(strstr(ot_error(rt), "heap exhausted"));
  check_int(rt, "(+ 20 22)", 42);
  check_error(rt, "((fn (x & args) args))", "arguments");
  check_error(rt, "(fn (x & x) x)", "duplicate");
  check_error(rt, "(fn (x . x) x)", "duplicate");
  check_error(rt, "(fn (x &) x)", "rest");
  check_error(rt, "(fn (x & y z) x)", "rest");
  check_error(rt, "(fn (x & y . z) x)", "rest");
  check_error(rt, "(fn (x . 1) x)", "rest");
  ot_destroy(rt);
}

TEST(apply_calls) {
  ot_state* rt = new_runtime(32768, 4096);
  ot_gc_stress(rt, true);
  check_int(rt, "(apply + '(1 2 3))", 6);
  check_int(rt, "(apply + 1 2 [3 4])", 10);
  check_int(rt, "(apply + nil)", 0);
  check_int(rt, "(apply apply (list + '(20 22)))", 42);
  check_int(rt, "(apply [42] '(0))", 42);
  check_int(rt, "(apply :answer (list {:answer 42}))", 42);
  check_int(rt, "(apply {:answer 42} [:answer])", 42);
  check_true(rt, "(equal? (apply list 1 2 '(3 4)) '(1 2 3 4))");
  check_true(rt, "(equal? (apply (fn args args) 1 '(2 3)) '(1 2 3))");
  check_int(
      rt,
      "(let ((x (cons 42 nil))) (apply (fn (p) (let ((garbage (cons 0 nil))) (car p))) (list x)))",
      42);
  check_int(rt,
            "(def saved-apply apply) (let ((change (fn () (set! apply (fn (a b) 99)) '(20 22)))) "
            "(saved-apply (fn (x) x) (list (apply + (change)))))",
            42);
  eval(rt, "(set! apply saved-apply)");
  check_int(rt,
            "(def (apply-loop n total) (if (= n 0) total (apply apply-loop (- n 1) (list (+ total "
            "1))))) (apply-loop 10000 0)",
            10000);
  CHECK(ot_get_stats(rt).max_frames < 5);
  check_int(rt,
            "(def (apply-rec n p) (if (= n 0) (car p) (+ (apply apply-rec (list (- n 1) (cons n "
            "nil))) (car p)))) (apply-rec 30 (cons 0 nil))",
            465);
  ot_value arguments[2];
  arguments[0] = eval(rt, "+");
  arguments[1] = eval(rt, "'(20 22)");
  ot_value answer;
  CHECK(ot_call(rt, "apply", arguments, 2, &answer) && answer == ot_int(42));
  check_error(rt, "(apply +)", "argument sequence");
  check_error(rt, "(apply list 1)", "proper list");
  check_error(rt, "(apply list '(1 . 2))", "proper list");
  check_error(rt, "(let ((p (cons 1 nil))) (set-cdr! p p) (apply list p))", "cycle");
  check_error(rt, "(apply 1 '())", "callable");
  check_error(rt, "(apply (fn (x) x) '())", "arguments");
  ot_destroy(rt);
}

TEST(apply_respects_stack_limit) {
  ot_state* rt = new_runtime(32768, 32);
  check_error(
      rt, "(let loop ((n 40) (xs '())) (if (= n 0) (apply list xs) (loop (- n 1) (cons n xs))))",
      "stack limit");
  ot_destroy(rt);
}

TEST(optional_arguments) {
  ot_state* rt = new_runtime(16384, 4096);
  ot_gc_stress(rt, true);
  check_int(rt, "((fn (x &optional (y (+ x 1)) (z (+ y 1))) (+ x y z)) 1)", 6);
  check_int(rt, "((fn (x &optional (y (+ x 1)) (z (+ y 1))) (+ x y z)) 1 20)", 42);
  check_int(rt, "(let ((x 41)) ((fn (&optional (x (+ x 1))) x)))", 42);
  CHECK(eval(rt, "((fn (&optional (x 42)) x) nil)") == OT_NIL);
  CHECK(eval(rt, "((fn (&optional (x 42)) x) #f)") == OT_FALSE);
  check_int(rt, "((fn (&optional (x (let ((a 20) (b 21)) (+ a b))) (y (+ x 1))) y))", 42);
  check_int(rt, "((fn (&optional (x (cons 41 nil)) (y (fn () (car x)))) (set-car! x 42) (y)))", 42);
  check_int(rt,
            "(def hits 0) (def (defaulted &optional (x (begin (set! hits (+ hits 1)) hits)) (y "
            "(begin (set! hits (+ hits 1)) hits))) (+ x y)) (defaulted) (defaulted 20 22) hits",
            2);
  check_int(rt, "(apply defaulted '(20 22))", 42);
  check_int(
      rt,
      "(def (optional-loop n &optional (x (cons n nil))) (if (= n 0) (car x) (optional-loop (- "
      "n 1)))) (optional-loop 10000)",
      0);
  CHECK(ot_get_stats(rt).max_frames < 5);
  check_int(rt,
            "(def (optional-rec n &optional (p (cons n nil))) (if (= n 0) (car p) (+ (optional-rec "
            "(- n 1)) (car p)))) (optional-rec 30)",
            465);
  check_int(rt,
            "(def (optional-keep n previous &optional (x (cons n nil))) (if (= n 0) (previous) "
            "(optional-keep (- n 1) (fn () (car x))))) (optional-keep 500 (fn () 99))",
            1);
  ot_value values[] = {ot_int(20), ot_int(22)}, result;
  CHECK(ot_call(rt, "defaulted", values, 2, &result) && result == ot_int(42));
  check_int(rt, "(def (optional-fast x &optional (y 1)) (+ x y)) (optional-fast 41)", 42);
  size_t collections = ot_get_stats(rt).collections;
  ot_value argument = ot_int(41);
  for (unsigned i = 0; i < 10000; i++)
    CHECK(ot_call(rt, "optional-fast", &argument, 1, &result) && result == ot_int(42));
  CHECK(ot_get_stats(rt).collections == collections);
  check_error(rt, "(defaulted 1 2 3)", "arguments");
  check_int(rt, "hits", 2);
  check_error(rt, "((fn (required &optional (x 1)) x))", "arguments");
  check_error(rt, "(fn (x &optional) x)", "optional");
  check_error(rt, "(fn (&optional x) x)", "default");
  check_error(rt, "(fn (&optional (x)) x)", "default");
  check_error(rt, "(fn (&optional (x 1 2)) x)", "default");
  check_error(rt, "(fn (&optional (x 1) & rest) x)", "default");
  check_error(rt, "(fn (&optional (x 1) . rest) x)", "rest");
  check_error(rt, "(fn (x &optional (x 1)) x)", "duplicate");
  check_error(rt, "(fn (&optional (x 1) (x 2)) x)", "duplicate");
  ot_destroy(rt);
}

TEST(frame_limit) {
  ot_state* rt = new_runtime(8192, 4096);
  CHECK(!ot_set_frame_limit(rt, 0));
  CHECK(ot_set_frame_limit(rt, 40));
  check_int(rt, "(def (depth n) (if (= n 0) 0 (+ 1 (depth (- n 1))))) (depth 30)", 30);
  check_error(rt, "(depth 100)", "frame depth limit");
  /* Tail calls reuse their frame. */
  check_int(rt, "(let loop ((i 1000)) (if (= i 0) 7 (loop (- i 1))))", 7);
  CHECK(ot_get_stats(rt).max_frames <= 40);
  ot_destroy(rt);
}

TEST(heap_statistics) {
  ot_state* rt = new_runtime(8192, 1024);
  ot_stats before = ot_get_stats(rt);
  check_int(rt, "(let loop ((i 1000) (p nil)) (if (= i 0) 1 (loop (- i 1) (cons i nil))))", 1);
  ot_stats after = ot_get_stats(rt);
  CHECK(after.collections > before.collections);
  CHECK(after.heap_allocated_bytes >= before.heap_allocated_bytes + 1000 * 24);
  CHECK(after.heap_reclaimed_bytes > before.heap_reclaimed_bytes);
  CHECK(after.heap_used <= after.heap_peak_used && after.heap_peak_used <= after.heap_capacity);
  CHECK(after.heap_allocated_bytes == after.heap_used + after.heap_reclaimed_bytes);
  ot_destroy(rt);
}

static const test_case tests[] = {
    TEST_ENTRY(frame_limit),
    TEST_ENTRY(heap_statistics),
    TEST_ENTRY(optional_arguments),
    TEST_ENTRY(apply_calls),
    TEST_ENTRY(apply_respects_stack_limit),
    TEST_ENTRY(rest_arguments),
    TEST_ENTRY(tables),
    TEST_ENTRY(table_garbage_and_exhaustion),
    TEST_ENTRY(arrays),
    TEST_ENTRY(array_garbage_and_exhaustion),
    TEST_ENTRY(long_list_equality),
    TEST_ENTRY(core_evaluation),
    TEST_ENTRY(tail_calls),
    TEST_ENTRY(collection_keeps_live_values),
    TEST_ENTRY(large_frame),
};

TEST_SUITE(runtime, tests);
