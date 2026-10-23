/* Otium platform layer for the QEMU virt machine: a 16550 UART console,
 * the time CSR and goldfish RTC for clocks, and no filesystem. */
#define OT_INTERNAL
#include "otium.h"
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

int console_read_byte(void) {
  while (!(UART_BASE[UART_STATUS] & UART_STATUS_READY)) {
  }
  return UART_BASE[UART_DATA];
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

bool ot_platform_read_file(const char* path, char** source, size_t* length) {
  (void)path;
  (void)source;
  (void)length;
  return false;
}

void ot_default_write(void* userdata, const char* bytes, size_t length) {
  (void)userdata;
  console_write(bytes, length);
}

/* Handles 0, 1 and 2 are the console; there are no files yet. */
static int io_open(void* data, const char* path, unsigned flags, intptr_t* handle) {
  (void)data;
  (void)path;
  (void)flags;
  (void)handle;
  return OT_IO_NOT_FOUND;
}

static long io_read(void* data, intptr_t handle, unsigned char* bytes, size_t length) {
  (void)data;
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
  (void)handle;
  return 0;
}

static int io_info(void* data, const char* path, ot_io_file_info* info) {
  (void)data;
  (void)path;
  (void)info;
  return OT_IO_NOT_FOUND;
}

static int io_remove(void* data, const char* path) {
  (void)data;
  (void)path;
  return OT_IO_NOT_FOUND;
}

static int io_rename(void* data, const char* from, const char* to) {
  (void)data;
  (void)from;
  (void)to;
  return OT_IO_NOT_FOUND;
}

static int io_mkdir(void* data, const char* path) {
  (void)data;
  (void)path;
  return OT_IO_PERMISSION;
}

static int io_list(void* data, const char* path, void (*emit)(void*, const char*), void* user) {
  (void)data;
  (void)path;
  (void)emit;
  (void)user;
  return OT_IO_NOT_FOUND;
}

static bool io_terminal(void* data, intptr_t handle) {
  (void)data;
  return handle >= 0 && handle <= 2;
}

static int io_symbolic_link(void* data, const char* path, bool* result) {
  (void)data;
  (void)path;
  (void)result;
  return OT_IO_NOT_FOUND;
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
