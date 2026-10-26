#include "runtime.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static ot_value eval(ot_runtime* rt, const char* source) {
  ot_value result;
  if (!ot_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s\n%s\n", source, ot_error(rt));
    abort();
  }
  return result;
}

int main(void) {
  ot_runtime* rt = ot_create(1024 * 1024, 128 * 1024);
  assert(rt);
  assert(ot_load_core(rt, "../lib"));
  ot_gc_stress(rt, true);
  FILE* file = fopen("../tests/library/bytevectors.ot", "rb");
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
      "(= (bytevector-length #u8(1 2)) 2)",
      "(let ((b #u8(1))) (and (not (eq? b (copy b))) (= (get (copy b) 0) 1)))",
      "(let ((b #u8(1))) (put! b 0 2 0 3) (= (get b 0) 3))",
      "(let ((b #u8(1))) (try (put! b 0 2 3 4) (catch e nil)) (= (get b 0) 2))",
      "(let ((b #u8(1))) (try (put! b 0 2 0) (catch e nil)) (= (get b 0) 1))",
      "(try (utf8->string #u8(255)) (catch e (and (condition-of-type? e 'decode-error) "
      "(condition-of-type? e 'io-error) (condition-of-type? e 'error))))",
      "(let ((b #u8(65 226 130 172))) (try (utf8->string b 1 3) (catch e (= (get e :position) "
      "1))))",
      "(let ((b #u8(65 226 130 172))) (try (utf8->string b 2) (catch e (= (get e :position) 2))))",
      "(equal? (utf8->string #u8(0 127 194 128 223 191 224 160 128 237 159 191 238 128 128 239 191 "
      "191 240 144 128 128 244 143 191 191)) (list->string (map integer->char '(0 127 128 2047 "
      "2048 55295 57344 65535 65536 1114111))))",
      "(let ((s (list->string (map integer->char '(0 127 128 2047 2048 55295 57344 65535 65536 "
      "1114111))))) (equal? s (utf8->string (string->utf8 s))))",
      "(begin (def saved-bytes (make-bytevector 4096 255)) (def saved-table (table saved-bytes "
      ":found)) (= (length (bytevector-append saved-bytes saved-bytes saved-bytes)) 12288))",
      "(and (eq? (get saved-table saved-bytes) :found) (= (bytevector-u8-ref saved-bytes 4095) "
      "255))"};
  for (size_t i = 0; i < sizeof(checks) / sizeof(*checks); i++)
    assert(eval(rt, checks[i]) == OT_TRUE);
  ot_destroy(rt);
  puts("bytevector collection and UTF-8 tests passed");
}
