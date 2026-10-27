#define _POSIX_C_SOURCE 200809L
#include "helpers.h"
#include <stdlib.h>
#include <unistd.h>

/* The script creates the directory tree in root that the later checks inspect. */
TEST(filesystem_metadata_and_symlinks) {
  char root[] = "/tmp/otium-fs-XXXXXX";
  CHECK(mkdtemp(root));
  ot_state* rt = new_core_runtime();
  ot_gc_stress(rt, true);
  const char* args[] = {root};
  CHECK(ot_set_arguments(rt, NULL, 1, args));
  eval_file(rt, "../tests/library/port-files.ot");
  char link[256], target[256];
  snprintf(link, sizeof(link), "%s/link", root);
  CHECK(!symlink("directory/a", link));
  snprintf(target, sizeof(target), "%s/dangling", root);
  CHECK(!symlink("missing", target));
  check_true(rt, "(and (file-regular? (str root \"/link\")) (file-symbolic-link? (str root "
                 "\"/link\")) (file-symbolic-link? (str root \"/dangling\")) (not (file-exists? "
                 "(str root \"/dangling\"))) (not (file-symbolic-link? (str root \"/absent\"))))");
  check_true(rt, "(and (path-absolute? (current-directory)) (equal? (list-directory root) "
                 "[\"dangling\" \"directory\" \"link\"]))");
  check_true(rt, "(try (file-info (str root \"/absent\")) (catch e (and (condition-of-type? e "
                 "'file-error) (eq? (get e :op) 'file-info) (eq? (get e :reason) :not-found))))");
  check_true(rt, "(delete-file (str root \"/link\")) (file-exists? (str root \"/directory/a\"))");
  check_true(rt, "(try (rename-file (str root \"/absent\") (str root \"/target\")) (catch e (eq? "
                 "(get e :reason) :not-found)))");
  check_true(rt, "(def nul-path (string-append root (str (integer->char 0)) \"/child\")) (try "
                 "(file-exists? nul-path) (catch e (equal? (condition-message e) \"expected path "
                 "without NUL\")))");
  CHECK(!unlink(target));
  snprintf(target, sizeof(target), "%s/directory/a", root);
  CHECK(!unlink(target));
  snprintf(target, sizeof(target), "%s/directory", root);
  CHECK(!rmdir(target));
  ot_destroy(rt);
  CHECK(!rmdir(root));
}

static const test_case tests[] = {
    TEST_ENTRY(filesystem_metadata_and_symlinks),
};

TEST_SUITE(filesystem, tests);
