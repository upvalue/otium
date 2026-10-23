#define _POSIX_C_SOURCE 200809L
#define OT_INTERNAL
#include "otium.h"

#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <limits.h>
#include <time.h>

void* ot_host_alloc(size_t size) { return calloc(1, size); }

void* ot_host_realloc(void* memory, size_t size) { return realloc(memory, size); }

void ot_host_free(void* memory) { free(memory); }

uint64_t ot_platform_monotonic_ns(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}

double ot_platform_current_second(void) {
  struct timespec now;
  clock_gettime(CLOCK_REALTIME, &now);
  return (double)now.tv_sec + (double)now.tv_nsec / 1e9;
}

bool ot_platform_read_file(const char* path, char** source, size_t* length) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) return false;
  size_t capacity = 4096;
  size_t used = 0;
  char* bytes = ot_host_alloc(capacity + 1);
  if (bytes == NULL) {
    fclose(file);
    return false;
  }
  for (;;) {
    if (used == capacity) {
      capacity *= 2;
      char* grown = ot_host_realloc(bytes, capacity + 1);
      if (grown == NULL) {
        ot_host_free(bytes);
        fclose(file);
        return false;
      }
      bytes = grown;
    }
    size_t got = fread(bytes + used, 1, capacity - used, file);
    used += got;
    if (got == 0) break;
  }
  bool ok = !ferror(file);
  fclose(file);
  if (!ok) {
    ot_host_free(bytes);
    return false;
  }
  bytes[used] = '\0';
  *source = bytes;
  *length = used;
  return true;
}

void ot_default_write(void* userdata, const char* bytes, size_t length) {
  FILE* file = userdata == NULL ? stdout : userdata;
  fwrite(bytes, 1, length, file);
}

static int io_errno(void) {
  switch (errno) {
    case ENOENT: return OT_IO_NOT_FOUND;
    case EEXIST: return OT_IO_EXISTS;
    case EACCES:
    case EPERM: return OT_IO_PERMISSION;
    case EISDIR: return OT_IO_IS_DIRECTORY;
    case ENOTDIR: return OT_IO_NOT_DIRECTORY;
    case ENOSPC: return OT_IO_NO_SPACE;
    case EBADF: return OT_IO_CLOSED;
    case EINTR: return OT_IO_INTERRUPTED;
    default: return OT_IO_OTHER;
  }
}

static int io_open(void* data, const char* path, unsigned flags, intptr_t* handle) {
  (void)data;
  int mode = flags & OT_IO_INPUT ? O_RDONLY : O_WRONLY | O_CREAT;
  if (flags & OT_IO_OUTPUT) mode |= flags & OT_IO_APPEND ? O_APPEND : O_TRUNC;
  if (flags & OT_IO_EXCLUSIVE) mode |= O_EXCL;
  int fd = open(path, mode, 0666);
  if (fd < 0) return io_errno();
  struct stat st;
  if (fstat(fd, &st) < 0) {
    int reason = io_errno();
    close(fd);
    return reason;
  }
  if (S_ISDIR(st.st_mode)) {
    close(fd);
    return OT_IO_IS_DIRECTORY;
  }
  *handle = fd;
  return 0;
}

static long io_read(void* data, intptr_t handle, unsigned char* bytes, size_t length) {
  (void)data;
  ssize_t n = read((int)handle, bytes, length > LONG_MAX ? LONG_MAX : length);
  return n < 0 ? -io_errno() : (long)n;
}

static long io_write(void* data, intptr_t handle, const unsigned char* bytes, size_t length) {
  (void)data;
  ssize_t n = write((int)handle, bytes, length > LONG_MAX ? LONG_MAX : length);
  return n < 0 ? -io_errno() : (long)n;
}

static int io_close(void* data, intptr_t handle) {
  (void)data;
  return close((int)handle) < 0 ? io_errno() : 0;
}

static int io_info(void* data, const char* path, ot_io_file_info* info) {
  (void)data;
  struct stat st;
  if (stat(path, &st) < 0) return io_errno();
  info->kind = S_ISREG(st.st_mode) ? 1 : S_ISDIR(st.st_mode) ? 2 : 0;
  info->size = (uint64_t)st.st_size;
  info->modified = (double)st.st_mtime;
  return 0;
}

static int io_remove(void* data, const char* path) {
  (void)data;
  struct stat st;
  if (lstat(path, &st) < 0) return io_errno();
  if (S_ISDIR(st.st_mode)) return OT_IO_IS_DIRECTORY;
  return unlink(path) < 0 ? io_errno() : 0;
}

static int io_rename(void* data, const char* from, const char* to) {
  (void)data;
  return rename(from, to) < 0 ? io_errno() : 0;
}

static int io_mkdir(void* data, const char* path) {
  (void)data;
  return mkdir(path, 0777) < 0 ? io_errno() : 0;
}

static int io_list(void* data, const char* path, void (*emit)(void*, const char*), void* user) {
  (void)data;
  DIR* directory = opendir(path);
  if (!directory) return io_errno();
  int reason = 0;
  for (;;) {
    errno = 0;
    struct dirent* entry = readdir(directory);
    if (!entry) {
      if (errno) reason = io_errno();
      break;
    }
    if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, "..")) emit(user, entry->d_name);
  }
  if (closedir(directory) < 0 && !reason) reason = io_errno();
  return reason;
}

static bool io_terminal(void* data, intptr_t handle) {
  (void)data;
  return isatty((int)handle);
}

static int io_symbolic_link(void* data, const char* path, bool* result) {
  (void)data;
  struct stat st;
  if (lstat(path, &st) < 0) return io_errno();
  *result = S_ISLNK(st.st_mode);
  return 0;
}

static int io_cwd(void* data, char** result) {
  (void)data;
  *result = getcwd(NULL, 0);
  return *result ? 0 : io_errno();
}

const ot_io_host* ot_platform_io_host(void) {
  static const ot_io_host host = {io_open, io_read,     io_write,         io_close,
                                  io_info, io_remove,   io_rename,        io_mkdir,
                                  io_list, io_terminal, io_symbolic_link, io_cwd};
  return &host;
}
