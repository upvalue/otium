#define _POSIX_C_SOURCE 200809L
#include "internal.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static ot_value eval(ot_runtime* rt, const char* source) {
  ot_value result;
  if (!ot_eval(rt, source, strlen(source), &result)) {
    fprintf(stderr, "%s\n%s\n", source, ot_error(rt));
    abort();
  }
  return result;
}

static void rejects(ot_runtime* rt, const char* source, const char* message) {
  ot_value result;
  if (ot_eval(rt, source, strlen(source), &result) || !strstr(ot_error(rt), message)) {
    fprintf(stderr, "expected %s: %s\n%s\n", message, source, ot_error(rt));
    abort();
  }
}

static void write_module(const char* directory, const char* name, const char* source) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ot", directory, name);
  FILE* f = fopen(path, "w");
  assert(f);
  assert(fputs(source, f) >= 0);
  assert(!fclose(f));
}

int main(void) {
  char directory[] = "/tmp/otium-modules-XXXXXX";
  assert(mkdtemp(directory));
  write_module(directory, "api", "(ns api) (def value 41) (def- secret 7) (def (read) value)");
  write_module(directory, "facade",
               "(ns facade (:require (api :as source :refer (read)))) (def (value) (source/read))");
  write_module(directory, "missingns", "(def partial 1)");
  for (size_t i = 0; i < 130; i++) {
    char name[32], source[128];
    snprintf(name, sizeof(name), "chain%zu", i);
    snprintf(source, sizeof(source), "(ns chain%zu) (require chain%zu)", i, i + 1);
    write_module(directory, name, source);
  }
  char large[512];
  snprintf(large, sizeof(large), "%s/oversized.ot", directory);
  FILE* large_file = fopen(large, "wb");
  assert(large_file);
  assert(!fseek(large_file, 16 * 1024 * 1024, SEEK_SET));
  assert(fputc(' ', large_file) != EOF);
  assert(!fclose(large_file));
  {
    ot_runtime* rt = ot_create(32768, 8192);
    assert(rt);
    ot_gc_stress(rt, true);
    assert(ot_add_load_path(rt, "../tests/modules/first"));
    assert(ot_add_load_path(rt, "../tests/modules/second"));
    assert(ot_add_load_path(rt, directory));
    assert(eval(rt, "(require choice suffix)") == OT_NIL);
    assert(eval(rt, "(equal? choice/value \"first-directory\")") == OT_TRUE);
    assert(eval(rt, "(equal? suffix/value \"ot\")") == OT_TRUE);
    rejects(rt, "(require retry)", "first load fails");
    assert(!strcmp(rt->namespaces[rt->current_namespace], "user"));
    eval(rt, "(require retry :as r :refer (value))");
    assert(eval(rt, "(value)") == ot_int(2));
    assert(eval(rt, "r/state") == OT_FALSE);
    eval(rt, "(def (caller) (r/value)) (def saved r/value) (set! r/value (fn () 42))");
    assert(eval(rt, "(caller)") == ot_int(42));
    assert(eval(rt, "(value)") == ot_int(42));
    assert(eval(rt, "(saved)") == ot_int(2));
    eval(rt, "(require (retry :as r :reload))");
    assert(eval(rt, "(caller)") == ot_int(3));
    assert(eval(rt, "(value)") == ot_int(3));
    eval(rt, "(require '(retry :as quoted :refer (value)))");
    assert(eval(rt, "(quoted/value)") == ot_int(3));
    size_t namespaces = rt->namespace_count, globals = rt->global_count;
    for (size_t i = 0; i < 1000; i++) eval(rt, "(require (retry :as r :refer (value)))");
    assert(rt->namespace_count == namespaces && rt->global_count == globals);
    for (size_t i = 0; i < 2; i++) {
      rejects(rt, "(require mismatch)", "namespace");
      rejects(rt, "(require cycle-a)", "circular");
      assert(eval(rt, "(require missingns) missingns/partial") == ot_int(1));
      assert(!strcmp(rt->namespaces[rt->current_namespace], "user"));
    }
    rejects(rt, "(def value 99)", "conflicts");
    rejects(rt, "(defonce value 99)", "conflicts");
    rejects(rt, "(require api :refer (secret))", "private");
    rejects(rt, "(require api :refer (missing))", "missing");
    rejects(rt, "(require api :as user)", "namespace name");
    eval(rt, "(ns consumer (:require (facade :as f) (api :refer (read))))");
    assert(eval(rt, "(read)") == ot_int(41));
    assert(eval(rt, "(f/value)") == ot_int(41));
    eval(rt, "(set! api/value 42)");
    assert(eval(rt, "(read)") == ot_int(42));
    assert(eval(rt, "(facade/read)") == ot_int(42));
    eval(rt, "(require ffi :as foreign)");
    assert(eval(rt, "(foreign/null? foreign/null)") == OT_TRUE);
    eval(rt, "(ns retarget) (require suffix :as s :refer (value)) "
             "(def (old) value) (require choice :as s :refer (value))");
    assert(eval(rt, "(equal? s/value \"first-directory\")") == OT_TRUE);
    assert(eval(rt, "(equal? value \"first-directory\")") == OT_TRUE);
    assert(eval(rt, "(equal? (old) \"ot\")") == OT_TRUE);
    const char* bad[] = {"(require api :as)",      "(require api :refer x)",
                         "(require api :unknown)", "(require (api extra))",
                         "(require api . x)",      "(fn () (require api))",
                         "(let () (require api))"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++) {
      ot_value result;
      assert(!ot_eval(rt, bad[i], strlen(bad[i]), &result));
    }
    eval(rt, "(def saved-api api/read) (def (live-api) (api/read))");
    write_module(directory, "api", "(ns api) (def value 50) (error \"reload failed\")");
    rejects(rt, "(require api :reload)", "reload failed");
    assert(eval(rt, "(live-api)") == ot_int(50));
    write_module(directory, "api", "(ns api) (def value 51) (def (read) (+ value 1))");
    eval(rt, "(require api)");
    assert(eval(rt, "(live-api)") == ot_int(52));
    assert(eval(rt, "(saved-api)") == ot_int(51));
    assert(eval(rt, "api/secret") == ot_int(7));
    char api_path[512];
    snprintf(api_path, sizeof(api_path), "%s/api.ot", directory);
    assert(!unlink(api_path));
    rejects(rt, "(require api :reload)", "not found");
    write_module(directory, "api", "(ns api) (def value 53) (def (read) (+ value 1))");
    assert(eval(rt, "(require api) (live-api)") == ot_int(54));
    assert(eval(rt, "(require)") == OT_NIL);
    assert(eval(rt, "(when #t 42)") == ot_int(42));
    assert(eval(rt, "(unless #t (error \"skipped\"))") == OT_NIL);
    eval(rt, "(def (tail n) (when (> n 0) (tail (- n 1))))");
    assert(eval(rt, "(tail 10000)") == OT_NIL);
    assert(ot_get_stats(rt).max_frames < 8);
    rejects(rt, "(require chain0)", "nesting limit");
    write_module(directory, "chain0", "(ns chain0) (def recovered 42)");
    assert(eval(rt, "(require chain0) chain0/recovered") == ot_int(42));
    write_module(directory, "chain0", "(ns chain0) (require chain1)");
    rejects(rt, "(require oversized)", "source size limit");
    printf("module tests passed\n");
    ot_destroy(rt);
    write_module(directory, "api", "(ns api) (def value 41) (def- secret 7) (def (read) value)");
  }
  const char* files[] = {"api", "facade", "missingns"};
  for (size_t i = 0; i < 3; i++) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ot", directory, files[i]);
    assert(!unlink(path));
  }
  for (size_t i = 0; i < 130; i++) {
    char path[512];
    snprintf(path, sizeof(path), "%s/chain%zu.ot", directory, i);
    assert(!unlink(path));
  }
  assert(!unlink(large));
  assert(!rmdir(directory));
}
