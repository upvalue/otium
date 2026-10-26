#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static Value eval(Runtime* rt, const char* source) {
  Value value;
  if (!runtime_eval(rt, source, strlen(source), &value)) {
    fprintf(stderr, "%s\n%s\n", source, runtime_error(rt));
    abort();
  }
  return value;
}

int main(void) {
  Runtime* rt = runtime_create(1024 * 1024, 128 * 1024);
  assert(rt);
  assert(runtime_load_core(rt, "../lib"));
  runtime_gc_stress(rt, true);
  FILE* file = fopen("../tests/library/ports.ot", "rb");
  assert(file);
  assert(!fseek(file, 0, SEEK_END));
  long n = ftell(file);
  assert(n >= 0);
  rewind(file);
  char* source = malloc((size_t)n + 1);
  assert(source);
  assert(fread(source, 1, (size_t)n, file) == (size_t)n);
  fclose(file);
  source[n] = 0;
  eval(rt, source);
  free(source);
  const char* checks[] = {
      "(let ((p (make-input-port \"restart\" {:read (fn (b s e) (invoke-restart 'done 42))}))) "
      "(let ((v (restart-case (read-u8 p) (done (v) v)))) (close-port p) (= v 42)))",
      "(let ((bytes (string->utf8 \"(a #\\\\) \\\"é\\\") :tail\")) (i 0) (p (make-input-port "
      "\"datum\" {:read (fn (b s e) (if (= i (length bytes)) (eof-object) (begin (put! b s (get "
      "bytes i)) (set! i (+ i 1)) 1)))}))) (let ((a (read p)) (b (read p))) (close-port p) (and "
      "(equal? a '(a #\\) \"é\")) (eq? b :tail))))",

      "(let ((p (open-output-string))) (not (contains? (open-ports) p)))",
      "(let ((p (make-output-port \"tracked\" {:write (fn (b s e) (- e s))}))) (let ((ports "
      "(open-ports))) (close-port p) (and (contains? ports p) (not (contains? (open-ports) p)))))",
      "(begin (def recursive-port nil) (set! recursive-port (make-input-port \"recursive\" {:read "
      "(fn (b s e) (read-u8 recursive-port))})) (try (read-u8 recursive-port) (catch e #t)))",
      "(begin (close-port recursive-port) (not (contains? (open-ports) recursive-port)))",
      "(let ((hits 0) (p (make-input-port \"retry input\" {:read (fn (b s e) (set! hits (+ hits "
      "1)) (if (= hits 1) (error :temporary) (begin (put! b s 65) 1)))}))) (try (read-char p) "
      "(catch e nil)) (let ((c (read-char p))) (close-port p) (eq? c #\\A)))",
      "(let ((n 0) (p (make-output-port \"growth\" {:write (fn (b s e) (set! n (+ n (- e s))) (- e "
      "s))}))) (write-string (string-repeat \"é\" 5000) p) (flush-output-port p) (close-port p) (= "
      "n 10000))",
      "(let ((p (make-input-port \"invalid count\" {:read (fn (b s e) (+ e 1))}))) (try (read-u8 "
      "p) (catch e (close-port p) #t)))",
      "(let ((p (make-output-port \"invalid count\" {:write (fn (b s e) 0)}))) (write-u8 1 p) (try "
      "(flush-output-port p) (catch e #t)))",
      "(let ((n 0) (p (make-output-port \"flush retry\" {:write (fn (b s e) (- e s)) :flush (fn () "
      "(set! n (+ n 1)) (when (= n 1) (error :retry)))}))) (write-u8 1 p) (try (flush-output-port "
      "p) (catch e nil)) (flush-output-port p) (close-port p) (= n 3))",

      "(let ((b #u8(65)) (p (open-input-bytevector b))) (put! b 0 66) (= (read-u8 p) 65))",
      "(let ((p (open-output-bytevector))) (write-bytevector #u8(1 2) p) (let ((b "
      "(get-output-bytevector p))) (put! b 0 9) (= (get (get-output-bytevector p) 0) 1)))",
      "(let ((p (open-output-string))) (write-string \"hello\" p) (let ((before (get-output-string "
      "p))) (dotimes (i 500) (write-string \"é🙂\" p)) (and (equal? before \"hello\") (= "
      "(string-length (get-output-string p)) 1005))))",
      "(equal? (with-output-to-string (fn () (with-params ((current-output-port "
      "(open-output-string)))) (display \"restored\"))) \"restored\")",
      "(equal? (with-output-to-string (fn () (try (with-output-to-string (fn () (error :bad))) "
      "(catch e (display \"caught\"))))) \"caught\")",
      "(with-params ((current-input-port (open-input-string \"é\"))) (and (= (peek-u8) 195) (eq? "
      "(read-char) #\\é) (eof-object? (read-u8))))",
      "(let ((p (open-input-bytevector #u8(65 255)))) (read-char p) (try (read-char p) (catch e "
      "(and (= (get e :position) 1) (= (read-u8 p) 255)))))",
      "(let ((p (open-output-string))) (write-u8 255 p) (try (get-output-string p) (catch e (eq? "
      "(condition-type e) 'decode-error))))",
      "(let ((p (open-input-string \"last\\r\"))) (equal? (read-line p) \"last\\r\"))",
      "(let ((p (open-input-string \"\"))) (and (equal? (read-all p) \"\") (eof-object? (read-line "
      "p)) (eof-object? (peek-u8 p))))",
      "(let ((p (open-input-string \"x\"))) (close-port p) (try (read-char p) (catch e (and "
      "(condition-of-type? e 'io-error) (eq? (get e :reason) :closed)))))",
      "(eq? (dispatch-type (open-output-string)) Port)",
      "(equal? (str (open-input-string \"\")) \"#<port \\\"string\\\" input>\")"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++)
    if (eval(rt, checks[i]) != V_TRUE) {
      fprintf(stderr, "failed: %s\n", checks[i]);
      abort();
    }
  eval(rt,
       "(def raw-source (open-input-bytevector #u8(1 2 3))) (def raw-target (make-bytevector 3))");
  uint32_t source_index = rt_lookup_global(rt, "raw-source", 10),
           target_index = rt_lookup_global(rt, "raw-target", 10);
  assert(source_index != UINT32_MAX && target_index != UINT32_MAX);
  Value args[] = {rt->globals[target_index], rt->globals[source_index]}, result;
  size_t collections = runtime_collections(rt);
  assert(runtime_call(rt, "read-bytevector!", args, 2, &result) && result == value_int(3));
  assert(runtime_collections(rt) == collections);
  runtime_destroy(rt);
  puts("memory port snapshots, UTF-8 and capture checks passed");
}
