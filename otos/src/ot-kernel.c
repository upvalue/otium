/* Otium platform layer for the QEMU virt machine: a 16550 UART console,
 * the time CSR and goldfish RTC for clocks, and the read-only files built
 * into the kernel image. */
#define OT_INTERNAL
#include "otium.h"
#include "files.h"
#include "kernel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UART_BASE ((volatile uint8_t*)0x10000000)
#define UART_DATA 0
#define UART_STATUS 5
#define UART_STATUS_READY 0x01
#define UART_STATUS_EMPTY 0x20

#define RTC_BASE ((volatile uint32_t*)0x101000)
/* QEMU virt's timebase. The device tree has the real value. */
#define TIMEBASE_HZ 10000000u

static void uart_put(char c) {
  while (!(UART_BASE[UART_STATUS] & UART_STATUS_EMPTY)) {
  }
  UART_BASE[UART_DATA] = (uint8_t)c;
}

void console_write(const char* bytes, size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (bytes[i] == '\n') uart_put('\r');
    uart_put(bytes[i]);
  }
}

/* Bytes that arrived while console_poll_interrupt was looking for ^C. */
static unsigned char typeahead[256];
static size_t typeahead_start, typeahead_count;

int console_read_byte(void) {
  if (typeahead_count > 0) {
    unsigned char c = typeahead[typeahead_start];
    typeahead_start = (typeahead_start + 1) % sizeof typeahead;
    typeahead_count--;
    return c;
  }
  while (!(UART_BASE[UART_STATUS] & UART_STATUS_READY)) {
  }
  return UART_BASE[UART_DATA];
}

bool console_poll_interrupt(void) {
  while (UART_BASE[UART_STATUS] & UART_STATUS_READY) {
    unsigned char c = UART_BASE[UART_DATA];
    if (c == 0x03) {
      typeahead_count = 0;
      return true;
    }
    if (typeahead_count < sizeof typeahead) {
      typeahead[(typeahead_start + typeahead_count) % sizeof typeahead] = c;
      typeahead_count++;
    }
  }
  return false;
}

/* picolibc's stdin, stdout and stderr all go to the UART. */
static int stdio_put(char c, FILE* file) {
  (void)file;
  console_write(&c, 1);
  return (unsigned char)c;
}

static int stdio_get(FILE* file) {
  (void)file;
  return console_read_byte();
}

static FILE console = FDEV_SETUP_STREAM(stdio_put, stdio_get, NULL, _FDEV_SETUP_RW);
FILE* const stdin = &console;
FILE* const stdout = &console;
FILE* const stderr = &console;

void* ot_host_alloc(size_t size) { return calloc(1, size); }

void* ot_host_realloc(void* memory, size_t size) { return realloc(memory, size); }

void ot_host_free(void* memory) { free(memory); }

static uint64_t read_time(void) {
  uint32_t high, low, again;
  do {
    __asm__ volatile("rdtimeh %0" : "=r"(high));
    __asm__ volatile("rdtime %0" : "=r"(low));
    __asm__ volatile("rdtimeh %0" : "=r"(again));
  } while (high != again);
  return ((uint64_t)high << 32) | low;
}

uint64_t ot_platform_monotonic_ns(void) {
  uint64_t ticks = read_time();
  return ticks / TIMEBASE_HZ * UINT64_C(1000000000) +
         ticks % TIMEBASE_HZ * UINT64_C(1000000000) / TIMEBASE_HZ;
}

double ot_platform_current_second(void) {
  /* Reading the low word latches the high word. */
  uint64_t low = RTC_BASE[0];
  uint64_t high = RTC_BASE[1];
  return (double)((high << 32) | low) / 1e9;
}

/* Writes path as an absolute path without ".", ".." or repeated slashes,
 * relative to the root since that is the only working directory. */
static bool resolve(const char* path, char* out, size_t capacity) {
  size_t length = 0;
  while (*path != '\0') {
    while (*path == '/') path++;
    const char* end = path;
    while (*end != '\0' && *end != '/') end++;
    size_t part = (size_t)(end - path);
    if (part == 0 || (part == 1 && path[0] == '.')) {
    } else if (part == 2 && path[0] == '.' && path[1] == '.') {
      while (length > 0 && out[length - 1] != '/') length--;
      if (length > 0) length--;
    } else {
      if (length + 1 + part + 1 > capacity) return false;
      out[length++] = '/';
      memcpy(out + length, path, part);
      length += part;
    }
    path = end;
  }
  if (length == 0) out[length++] = '/';
  out[length] = '\0';
  return true;
}

static const embedded_file* find_file(const char* path) {
  char resolved[256];
  if (!resolve(path, resolved, sizeof resolved)) return NULL;
  for (size_t i = 0; i < embedded_file_count; i++)
    if (strcmp(embedded_files[i].path, resolved) == 0) return &embedded_files[i];
  return NULL;
}

/* A directory is any prefix of a file's path that ends at a slash. Returns the
 * resolved path's length, or 0 when it is not a directory. */
static size_t find_directory(const char* path, char* resolved, size_t capacity) {
  if (!resolve(path, resolved, capacity)) return 0;
  size_t length = strcmp(resolved, "/") == 0 ? 0 : strlen(resolved);
  for (size_t i = 0; i < embedded_file_count; i++) {
    const char* file = embedded_files[i].path;
    if (strncmp(file, resolved, length) == 0 && file[length] == '/') return length == 0 ? 1 : length;
  }
  return 0;
}

bool ot_platform_read_file(const char* path, char** source, size_t* length) {
  const embedded_file* file = find_file(path);
  if (file == NULL) return false;
  *source = ot_host_alloc(file->length + 1);
  if (*source == NULL) return false;
  memcpy(*source, file->data, file->length + 1);
  *length = file->length;
  return true;
}

void ot_default_write(void* userdata, const char* bytes, size_t length) {
  (void)userdata;
  console_write(bytes, length);
}

/* Handles 0, 1 and 2 are the console; open files start after them. */
#define FIRST_FILE_HANDLE 3

static struct {
  const embedded_file* file;
  size_t offset;
} open_files[16];

static int io_open(void* data, const char* path, unsigned flags, intptr_t* handle) {
  (void)data;
  const embedded_file* file = find_file(path);
  char resolved[256];
  if (file == NULL) {
    if (find_directory(path, resolved, sizeof resolved)) return OT_IO_IS_DIRECTORY;
    return flags & OT_IO_OUTPUT ? OT_IO_PERMISSION : OT_IO_NOT_FOUND;
  }
  if (flags & OT_IO_OUTPUT) return OT_IO_PERMISSION;
  for (size_t i = 0; i < sizeof open_files / sizeof open_files[0]; i++) {
    if (open_files[i].file != NULL) continue;
    open_files[i].file = file;
    open_files[i].offset = 0;
    *handle = (intptr_t)(FIRST_FILE_HANDLE + i);
    return 0;
  }
  return OT_IO_NO_SPACE;
}

static bool open_file(intptr_t handle, size_t* index) {
  if (handle < FIRST_FILE_HANDLE) return false;
  *index = (size_t)(handle - FIRST_FILE_HANDLE);
  return *index < sizeof open_files / sizeof open_files[0] && open_files[*index].file != NULL;
}

static long io_read(void* data, intptr_t handle, unsigned char* bytes, size_t length) {
  (void)data;
  size_t index;
  if (open_file(handle, &index)) {
    const embedded_file* file = open_files[index].file;
    size_t left = file->length - open_files[index].offset;
    if (length > left) length = left;
    memcpy(bytes, file->data + open_files[index].offset, length);
    open_files[index].offset += length;
    return (long)length;
  }
  if (handle != 0) return -OT_IO_CLOSED;
  if (length == 0) return 0;
  bytes[0] = (unsigned char)console_read_byte();
  return 1;
}

static long io_write(void* data, intptr_t handle, const unsigned char* bytes, size_t length) {
  (void)data;
  if (handle != 1 && handle != 2) return -OT_IO_CLOSED;
  console_write((const char*)bytes, length);
  return (long)length;
}

static int io_close(void* data, intptr_t handle) {
  (void)data;
  size_t index;
  if (open_file(handle, &index)) open_files[index].file = NULL;
  return 0;
}

static int io_info(void* data, const char* path, ot_io_file_info* info) {
  (void)data;
  const embedded_file* file = find_file(path);
  char resolved[256];
  if (file != NULL) {
    *info = (ot_io_file_info){.kind = 1, .size = file->length};
  } else if (find_directory(path, resolved, sizeof resolved)) {
    *info = (ot_io_file_info){.kind = 2};
  } else {
    return OT_IO_NOT_FOUND;
  }
  return 0;
}

static int io_remove(void* data, const char* path) {
  (void)data;
  (void)path;
  return OT_IO_PERMISSION;
}

static int io_rename(void* data, const char* from, const char* to) {
  (void)data;
  (void)from;
  (void)to;
  return OT_IO_PERMISSION;
}

static int io_mkdir(void* data, const char* path) {
  (void)data;
  (void)path;
  return OT_IO_PERMISSION;
}

/* Emits each name directly inside the directory once. The files are sorted,
 * so repeats of a subdirectory's name are adjacent. */
static int io_list(void* data, const char* path, void (*emit)(void*, const char*), void* user) {
  (void)data;
  char resolved[256];
  size_t length = find_directory(path, resolved, sizeof resolved);
  if (length == 0) return find_file(path) ? OT_IO_NOT_DIRECTORY : OT_IO_NOT_FOUND;
  if (strcmp(resolved, "/") == 0) length = 0;
  char previous[256] = "";
  for (size_t i = 0; i < embedded_file_count; i++) {
    const char* file = embedded_files[i].path;
    if (strncmp(file, resolved, length) != 0 || file[length] != '/') continue;
    const char* name = file + length + 1;
    size_t name_length = strcspn(name, "/");
    if (name_length >= sizeof previous) continue;
    if (strncmp(previous, name, name_length) == 0 && previous[name_length] == '\0') continue;
    memcpy(previous, name, name_length);
    previous[name_length] = '\0';
    emit(user, previous);
  }
  return 0;
}

static bool io_terminal(void* data, intptr_t handle) {
  (void)data;
  return handle >= 0 && handle <= 2;
}

static int io_symbolic_link(void* data, const char* path, bool* result) {
  (void)data;
  char resolved[256];
  if (find_file(path) == NULL && !find_directory(path, resolved, sizeof resolved))
    return OT_IO_NOT_FOUND;
  *result = false;
  return 0;
}

static int io_cwd(void* data, char** result) {
  (void)data;
  *result = ot_host_alloc(2);
  if (*result == NULL) return OT_IO_NO_SPACE;
  (*result)[0] = '/';
  return 0;
}

const ot_io_host* ot_platform_io_host(void) {
  static const ot_io_host host = {io_open, io_read,     io_write,         io_close,
                                  io_info, io_remove,   io_rename,        io_mkdir,
                                  io_list, io_terminal, io_symbolic_link, io_cwd};
  return &host;
}
