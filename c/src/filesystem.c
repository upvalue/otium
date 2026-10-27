#include "internal.h"
#include <errno.h>
#ifndef OTIUM_FREESTANDING
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool path_valid(ot_value path) {
  return oti_kind(path, K_STRING) && !memchr(oti_bytes_data(path), 0, oti_bytes_length(path));
}

static int compare_names(const void* a, const void* b) {
  return strcmp(*(const char* const*)a, *(const char* const*)b);
}

static ot_value directory(ot_state* rt, ot_value path) {
  DIR* dir = opendir((char*)oti_bytes_data(path));
  if (!dir) return oti_file_error(rt, "list-directory", path, errno);
  char** names = NULL;
  size_t count = 0, capacity = 0;
  int error = 0;
  bool ok = true;
  while (ok) {
    if (!ot_poll_interrupt(rt)) {
      ok = false;
      break;
    }
    errno = 0;
    struct dirent* entry = readdir(dir);
    if (!entry) {
      error = errno;
      break;
    }
    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
    if (count == UINT32_MAX) {
      oti_fail(rt, "directory too large");
      ok = false;
      break;
    }
    if (!oti_grow(rt, (void**)&names, &capacity, count + 1, sizeof(char*))) {
      ok = false;
      break;
    }
    names[count] = strdup(entry->d_name);
    if (!names[count]) {
      oti_fail(rt, "out of host memory");
      ok = false;
      break;
    }
    count++;
  }
  if (closedir(dir) && !error) error = errno;
  ot_value live[] = {path, OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 3);
  if (error) {
    oti_file_error(rt, "list-directory", live[0], error);
    ok = false;
  }
  if (ok) {
    if (count > 1) qsort(names, count, sizeof(char*), compare_names);
    live[1] = oti_alloc(rt, K_ARRAY, 1, OTI_BYTES(oti_array_obj, 1));
    ok = live[1] != 0;
  }
  if (ok && count) {
    ot_value storage = oti_alloc(rt, K_ARRAY_DATA, (uint32_t)count, 0);
    ok = storage != 0;
    if (ok) {
      oti_as_array(live[1])->data = storage;
      oti_as_array(live[1])->capacity = count;
    }
  }
  for (size_t i = 0; ok && i < count; i++) {
    live[2] = oti_text(rt, K_STRING, names[i], strlen(names[i]));
    ok = live[2] != 0;
    if (ok) {
      object_fields(oti_as_array(live[1])->data)[i] = live[2];
      oti_as_array(live[1])->length = i + 1;
    }
  }
  for (size_t i = 0; i < count; i++) free(names[i]);
  free(names);
  ot_value result = ok ? live[1] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value metadata(ot_state* rt, const struct stat* st) {
  if (st->st_size < 0 || (uintmax_t)st->st_size > (uintmax_t)(INTPTR_MAX / 2))
    return oti_fail(rt, "file size exceeds integer range");
  ot_value live[] = {OT_NIL, OT_NIL};
  oti_roots roots;
  oti_roots_push(&rt->heap, &roots, live, 2);
  live[0] = oti_table_new(rt);
  bool ok = live[0] != 0;
  const char* keys[] = {":type", ":size", ":modified"};
  for (size_t i = 0; ok && i < 3; i++) {
    if (!i) {
      const char* type = S_ISREG(st->st_mode)   ? ":file"
                         : S_ISDIR(st->st_mode) ? ":directory"
                                                : ":other";
      live[1] = oti_intern(rt, K_KEYWORD, type, strlen(type));
    } else if (i == 1) live[1] = ot_int((intptr_t)st->st_size);
    else live[1] = oti_float(rt, (double)st->st_mtime);
    ot_value key = oti_intern(rt, K_KEYWORD, keys[i], strlen(keys[i]));
    ok = live[1] && key && oti_table_set(rt, live[0], key, live[1]);
  }
  ot_value result = ok ? live[0] : 0;
  oti_roots_pop(&rt->heap, &roots);
  return result;
}

static ot_value filesystem_current_directory(ot_state* rt, size_t first, size_t count) {
  (void)first;
  if (count) return oti_fail(rt, "current-directory takes no arguments");
  size_t size = 256;
  char* buffer = NULL;
  for (;;) {
    buffer = malloc(size);
    if (!buffer) return oti_fail(rt, "out of host memory");
    if (getcwd(buffer, size)) break;
    int error = errno;
    free(buffer);
    if (error != ERANGE) return oti_file_error(rt, "current-directory", OT_NIL, error);
    if (size > SIZE_MAX / 2) return oti_fail(rt, "directory path too long");
    size *= 2;
  }
  ot_value result = oti_text(rt, K_STRING, buffer, strlen(buffer));
  free(buffer);
  return result;
}

/* Check that there are `expected` arguments and that each is a path. */
static bool filesystem_paths(ot_state* rt, size_t first, size_t count, size_t expected) {
  if (count != expected) {
    oti_fail(rt, "wrong number of filesystem arguments");
    return false;
  }
  for (size_t i = 0; i < count; i++)
    if (!path_valid(rt->stack[first + i])) {
      oti_fail(rt, "expected path without NUL");
      return false;
    }
  return true;
}

/* The result of a call that returns zero or sets errno. */
static ot_value filesystem_done(ot_state* rt, const char* op, ot_value path, int result) {
  return result ? oti_file_error(rt, op, path, errno) : OT_NIL;
}

static ot_value filesystem_file_exists(ot_state* rt, size_t first, size_t count) {
  if (!filesystem_paths(rt, first, count, 1)) return 0;
  ot_value path = rt->stack[first];
  struct stat st;
  if (stat((char*)oti_bytes_data(path), &st)) {
    int error = errno;
    if (error == ENOENT || error == ENOTDIR) return OT_FALSE;
    return oti_file_error(rt, "file-exists?", path, error);
  }
  return OT_TRUE;
}

static ot_value filesystem_file_link(ot_state* rt, size_t first, size_t count) {
  if (!filesystem_paths(rt, first, count, 1)) return 0;
  ot_value path = rt->stack[first];
  struct stat st;
  if (lstat((char*)oti_bytes_data(path), &st)) {
    int error = errno;
    if (error == ENOENT || error == ENOTDIR) return OT_FALSE;
    return oti_file_error(rt, "file-symbolic-link?", path, error);
  }
  return S_ISLNK(st.st_mode) ? OT_TRUE : OT_FALSE;
}

static ot_value filesystem_file_info(ot_state* rt, size_t first, size_t count) {
  if (!filesystem_paths(rt, first, count, 1)) return 0;
  ot_value path = rt->stack[first];
  struct stat st;
  if (stat((char*)oti_bytes_data(path), &st)) return oti_file_error(rt, "file-info", path, errno);
  return metadata(rt, &st);
}

static ot_value filesystem_list_directory(ot_state* rt, size_t first, size_t count) {
  if (!filesystem_paths(rt, first, count, 1)) return 0;
  return directory(rt, rt->stack[first]);
}

static ot_value filesystem_delete_file(ot_state* rt, size_t first, size_t count) {
  if (!filesystem_paths(rt, first, count, 1)) return 0;
  ot_value path = rt->stack[first];
  const char* name = (char*)oti_bytes_data(path);
  if (!unlink(name)) return OT_NIL;
  /* macOS reports a directory as EPERM, which POSIX allows; Linux says EISDIR. */
  int error = errno;
  struct stat st;
  if (error == EPERM && !lstat(name, &st) && S_ISDIR(st.st_mode)) error = EISDIR;
  return oti_file_error(rt, "delete-file", path, error);
}

static ot_value filesystem_rename_file(ot_state* rt, size_t first, size_t count) {
  if (!filesystem_paths(rt, first, count, 2)) return 0;
  ot_value from = rt->stack[first], to = rt->stack[first + 1];
  int result = rename((char*)oti_bytes_data(from), (char*)oti_bytes_data(to));
  return filesystem_done(rt, "rename-file", from, result);
}

static ot_value filesystem_make_directory(ot_state* rt, size_t first, size_t count) {
  if (!filesystem_paths(rt, first, count, 1)) return 0;
  ot_value path = rt->stack[first];
  return filesystem_done(rt, "make-directory", path, mkdir((char*)oti_bytes_data(path), 0777));
}

#else
/* Without an operating system the only files are the I/O host's, which has no
 * metadata or directory calls, so these fail as unsupported. */
static ot_value unsupported(ot_state* rt, const char* op, size_t first, size_t count) {
  return oti_file_error(rt, op, count ? rt->stack[first] : OT_NIL, ENOSYS);
}

#define UNSUPPORTED(name, op)                                                                      \
  static ot_value name(ot_state* rt, size_t first, size_t count) {                                 \
    return unsupported(rt, op, first, count);                                                      \
  }
UNSUPPORTED(filesystem_file_exists, "file-exists?")
UNSUPPORTED(filesystem_delete_file, "delete-file")
UNSUPPORTED(filesystem_rename_file, "rename-file")
UNSUPPORTED(filesystem_file_info, "file-info")
UNSUPPORTED(filesystem_list_directory, "list-directory")
UNSUPPORTED(filesystem_make_directory, "make-directory")
UNSUPPORTED(filesystem_file_link, "file-symbolic-link?")
UNSUPPORTED(filesystem_current_directory, "current-directory")
#endif

const oti_builtin_def oti_filesystem_builtins[] = {
    {B_FILE_EXISTS, "file-exists?", filesystem_file_exists},
    {B_DELETE_FILE, "delete-file", filesystem_delete_file},
    {B_RENAME_FILE, "rename-file", filesystem_rename_file},
    {B_FILE_INFO, "file-info", filesystem_file_info},
    {B_LIST_DIRECTORY, "list-directory", filesystem_list_directory},
    {B_MAKE_DIRECTORY, "make-directory", filesystem_make_directory},
    {B_FILE_LINK, "file-symbolic-link?", filesystem_file_link},
    {B_CURRENT_DIRECTORY, "current-directory", filesystem_current_directory},
    {0, NULL, NULL},
};
