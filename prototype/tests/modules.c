#define _POSIX_C_SOURCE 200809L
#include "helpers.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_module(const char* directory, const char* name, const char* source) {
  char path[512];
  snprintf(path, sizeof(path), "%s/%s.ot", directory, name);
  FILE* f = fopen(path, "w");
  CHECK(f);
  CHECK(fputs(source, f) >= 0);
  CHECK(!fclose(f));
}

static void write_fixtures(const char* directory, char* large, size_t large_size) {
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
  snprintf(large, large_size, "%s/oversized.ot", directory);
  FILE* large_file = fopen(large, "wb");
  CHECK(large_file);
  CHECK(!fseek(large_file, 16 * 1024 * 1024, SEEK_SET));
  CHECK(fputc(' ', large_file) != EOF);
  CHECK(!fclose(large_file));
}

static void remove_fixtures(const char* directory, const char* large) {
  const char* files[] = {"api", "facade", "missingns"};
  for (size_t i = 0; i < 3; i++) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ot", directory, files[i]);
    CHECK(!unlink(path));
  }
  for (size_t i = 0; i < 130; i++) {
    char path[512];
    snprintf(path, sizeof(path), "%s/chain%zu.ot", directory, i);
    CHECK(!unlink(path));
  }
  CHECK(!unlink(large));
  CHECK(!rmdir(directory));
}

static void check_in_user(ot_state* rt) {
  CHECK_STR(rt->namespaces[rt->current_namespace], "user");
}

/* One runtime throughout: each step depends on the modules loaded and
 * rewritten by the steps before it. */
TEST(modules_load_refer_and_reload) {
  char directory[] = "/tmp/otium-modules-XXXXXX";
  CHECK(mkdtemp(directory));
  char large[512];
  write_fixtures(directory, large, sizeof(large));

  ot_state* rt = new_runtime(32768, 8192);
  ot_gc_stress(rt, true);
  CHECK(ot_add_load_path(rt, "../tests/modules/first"));
  CHECK(ot_add_load_path(rt, "../tests/modules/second"));
  CHECK(ot_add_load_path(rt, directory));
  CHECK(eval(rt, "(require choice suffix)") == OT_NIL);
  check_true(rt, "(equal? choice/value \"first-directory\")");
  check_true(rt, "(equal? suffix/value \"ot\")");
  check_error(rt, "(require retry)", "first load fails");
  check_in_user(rt);
  eval(rt, "(require retry :as r :refer (value))");
  check_int(rt, "(value)", 2);
  CHECK(eval(rt, "r/state") == OT_FALSE);
  eval(rt, "(def (caller) (r/value)) (def saved r/value) (set! r/value (fn () 42))");
  check_int(rt, "(caller)", 42);
  check_int(rt, "(value)", 42);
  check_int(rt, "(saved)", 2);
  eval(rt, "(require (retry :as r :reload))");
  check_int(rt, "(caller)", 3);
  check_int(rt, "(value)", 3);
  eval(rt, "(require '(retry :as quoted :refer (value)))");
  check_int(rt, "(quoted/value)", 3);
  size_t namespaces = rt->namespace_count, globals = rt->global_count;
  for (size_t i = 0; i < 1000; i++) eval(rt, "(require (retry :as r :refer (value)))");
  CHECK(rt->namespace_count == namespaces && rt->global_count == globals);
  for (size_t i = 0; i < 2; i++) {
    check_error(rt, "(require mismatch)", "namespace");
    check_error(rt, "(require cycle-a)", "circular");
    check_int(rt, "(require missingns) missingns/partial", 1);
    check_in_user(rt);
  }
  check_error(rt, "(def value 99)", "conflicts");
  check_error(rt, "(defonce value 99)", "conflicts");
  check_error(rt, "(require api :refer (secret))", "private");
  check_error(rt, "(require api :refer (missing))", "missing");
  check_error(rt, "(require api :as user)", "namespace name");
  eval(rt, "(ns consumer (:require (facade :as f) (api :refer (read))))");
  check_int(rt, "(read)", 41);
  check_int(rt, "(f/value)", 41);
  eval(rt, "(set! api/value 42)");
  check_int(rt, "(read)", 42);
  check_int(rt, "(facade/read)", 42);
  eval(rt, "(require ffi :as foreign)");
  check_true(rt, "(foreign/null? foreign/null)");
  eval(rt, "(ns retarget) (require suffix :as s :refer (value)) "
           "(def (old) value) (require choice :as s :refer (value))");
  check_true(rt, "(equal? s/value \"first-directory\")");
  check_true(rt, "(equal? value \"first-directory\")");
  check_true(rt, "(equal? (old) \"ot\")");
  const char* nested = "form not implemented in the prototype compiler";

  const struct {
    const char* source;
    const char* message;
  } bad[] = {{"(require api :as)", "require :as needs one alias"},
             {"(require api :refer x)", "require :refer needs a name list"},
             {"(require api :unknown)", "unknown require option"},
             {"(require (api extra))", "invalid grouped require specification"},
             {"(require api . x)", "require needs a proper specification list"},
             {"(fn () (require api))", nested},
             {"(let () (require api))", nested}};

  for (size_t i = 0; i < sizeof(bad) / sizeof(*bad); i++)
    check_error(rt, bad[i].source, bad[i].message);
  eval(rt, "(def saved-api api/read) (def (live-api) (api/read))");
  write_module(directory, "api", "(ns api) (def value 50) (error \"reload failed\")");
  check_error(rt, "(require api :reload)", "reload failed");
  check_int(rt, "(live-api)", 50);
  write_module(directory, "api", "(ns api) (def value 51) (def (read) (+ value 1))");
  eval(rt, "(require api)");
  check_int(rt, "(live-api)", 52);
  check_int(rt, "(saved-api)", 51);
  check_int(rt, "api/secret", 7);
  char api_path[512];
  snprintf(api_path, sizeof(api_path), "%s/api.ot", directory);
  CHECK(!unlink(api_path));
  check_error(rt, "(require api :reload)", "not found");
  write_module(directory, "api", "(ns api) (def value 53) (def (read) (+ value 1))");
  check_int(rt, "(require api) (live-api)", 54);
  CHECK(eval(rt, "(require)") == OT_NIL);
  check_int(rt, "(when #t 42)", 42);
  CHECK(eval(rt, "(unless #t (error \"skipped\"))") == OT_NIL);
  eval(rt, "(def (tail n) (when (> n 0) (tail (- n 1))))");
  CHECK(eval(rt, "(tail 10000)") == OT_NIL);
  CHECK(ot_get_stats(rt).max_frames < 8);
  check_error(rt, "(require chain0)", "nesting limit");
  write_module(directory, "chain0", "(ns chain0) (def recovered 42)");
  check_int(rt, "(require chain0) chain0/recovered", 42);
  write_module(directory, "chain0", "(ns chain0) (require chain1)");
  check_error(rt, "(require oversized)", "source size limit");
  ot_destroy(rt);

  remove_fixtures(directory, large);
}

static const test_case tests[] = {
    TEST_ENTRY(modules_load_refer_and_reload),
};

TEST_MAIN(tests)
