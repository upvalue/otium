/* Otium platform layer for the QEMU virt machine: a 16550 UART console, the
 * time CSR and goldfish RTC for clocks, and the read-only files built into the
 * kernel image, served to the runtime through its I/O host. */
#define _POSIX_C_SOURCE 200809L
#include "otium.h"
#include "files.h"
#include "kernel.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define UART_BASE ((volatile uint8_t*)0x10000000)
#define UART_DATA 0
#define UART_INTERRUPTS 1
#define UART_MODEM_CONTROL 4
#define UART_STATUS 5
#define UART_STATUS_READY 0x01
#define UART_STATUS_EMPTY 0x20
#define UART_IRQ 10

/* The PLIC's registers for hart 0 in supervisor mode, its context 1. */
#define PLIC_BASE 0x0c000000u
#define PLIC_PRIORITY(irq) ((volatile uint32_t*)(PLIC_BASE + 4 * (irq)))
#define PLIC_ENABLE ((volatile uint32_t*)(PLIC_BASE + 0x2080))
#define PLIC_THRESHOLD ((volatile uint32_t*)(PLIC_BASE + 0x201000))
#define PLIC_CLAIM ((volatile uint32_t*)(PLIC_BASE + 0x201004))

#define SIE_EXTERNAL (1u << 9)
#define SSTATUS_SIE (1u << 1)
#define CAUSE_EXTERNAL 9

#define RTC_BASE ((volatile uint32_t*)0x101000)
/* QEMU virt's timebase. The device tree has the real value. */
#define TIMEBASE_HZ 10000000u

static void uart_put(char c) {
  while (!(UART_BASE[UART_STATUS] & UART_STATUS_EMPTY)) {}
  UART_BASE[UART_DATA] = (uint8_t)c;
}

void console_write(const char* bytes, size_t length) {
  for (size_t i = 0; i < length; i++) {
    if (bytes[i] == '\n') uart_put('\r');
    uart_put(bytes[i]);
  }
}

/* Input bytes, filled by the UART interrupt and emptied by console_read_byte. */
static unsigned char input[256];
static volatile size_t input_head, input_tail;

volatile sig_atomic_t console_interrupt;
volatile bool console_evaluating;

void console_init(void) {
  *PLIC_PRIORITY(UART_IRQ) = 1;
  *PLIC_ENABLE |= 1u << UART_IRQ;
  *PLIC_THRESHOLD = 0;
  UART_BASE[UART_MODEM_CONTROL] |= 0x08; /* OUT2 routes the UART's interrupt out. */
  UART_BASE[UART_INTERRUPTS] = 0x01;     /* Received data available. */
  __asm__ volatile("csrs sie, %0" : : "r"(SIE_EXTERNAL));
  __asm__ volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE));
}

/* While Otium code runs, ^C sets the interrupt flag and drops typeahead.
 * Otherwise it reaches the line editor like any other byte. */
static void uart_receive(void) {
  while (UART_BASE[UART_STATUS] & UART_STATUS_READY) {
    unsigned char c = UART_BASE[UART_DATA];
    if (c == 0x03 && console_evaluating) {
      console_interrupt = 1;
      input_tail = input_head;
      continue;
    }
    size_t next = (input_head + 1) % sizeof input;
    if (next == input_tail) continue;
    input[input_head] = c;
    input_head = next;
  }
}

void kernel_interrupt(uintptr_t cause) {
  if ((cause & 0xff) != CAUSE_EXTERNAL) return;
  uint32_t irq = *PLIC_CLAIM;
  if (irq == UART_IRQ) uart_receive();
  if (irq != 0) *PLIC_CLAIM = irq;
}

int console_read_byte(void) {
  for (;;) {
    /* With interrupts off, a byte can't arrive between the check and wfi;
     * wfi still wakes for the pending interrupt. */
    __asm__ volatile("csrc sstatus, %0" : : "r"(SSTATUS_SIE));
    bool empty = input_tail == input_head;
    bool interrupted = console_evaluating && console_interrupt;
    if (empty && !interrupted) __asm__ volatile("wfi");
    __asm__ volatile("csrs sstatus, %0" : : "r"(SSTATUS_SIE));
    if (interrupted) return -1;
    if (!empty) break;
  }
  unsigned char c = input[input_tail];
  input_tail = (input_tail + 1) % sizeof input;
  return c;
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

static uint64_t read_time(void) {
  uint32_t high, low, again;
  do {
    __asm__ volatile("rdtimeh %0" : "=r"(high));
    __asm__ volatile("rdtime %0" : "=r"(low));
    __asm__ volatile("rdtimeh %0" : "=r"(again));
  } while (high != again);
  return ((uint64_t)high << 32) | low;
}

/* picolibc declares clock_gettime but leaves it to the platform. The monotonic
 * clock counts from boot; the real-time clock is the goldfish RTC. */
int clock_gettime(clockid_t clock, struct timespec* now) {
  if (clock == CLOCK_MONOTONIC) {
    uint64_t ticks = read_time();
    now->tv_sec = (time_t)(ticks / TIMEBASE_HZ);
    now->tv_nsec = (long)(ticks % TIMEBASE_HZ * (1000000000u / TIMEBASE_HZ));
    return 0;
  }
  if (clock == CLOCK_REALTIME) {
    /* Reading the low word latches the high word. */
    uint64_t low = RTC_BASE[0];
    uint64_t high = RTC_BASE[1];
    uint64_t ns = (high << 32) | low;
    now->tv_sec = (time_t)(ns / 1000000000u);
    now->tv_nsec = (long)(ns % 1000000000u);
    return 0;
  }
  errno = EINVAL;
  return -1;
}

uint64_t kernel_uptime_ns(void) {
  struct timespec now;
  clock_gettime(CLOCK_MONOTONIC, &now);
  return (uint64_t)now.tv_sec * 1000000000u + (uint64_t)now.tv_nsec;
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

/* A directory is any prefix of a file's path that ends at a slash. */
static bool is_directory(const char* path) {
  char resolved[256];
  if (!resolve(path, resolved, sizeof resolved)) return false;
  size_t length = strcmp(resolved, "/") == 0 ? 0 : strlen(resolved);
  for (size_t i = 0; i < embedded_file_count; i++) {
    const char* file = embedded_files[i].path;
    if (strncmp(file, resolved, length) == 0 && file[length] == '/') return true;
  }
  return false;
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
  if (file == NULL) {
    if (is_directory(path)) return EISDIR;
    return flags & OT_IO_WRITE ? EROFS : ENOENT;
  }
  if (flags & OT_IO_WRITE) return EROFS;
  for (size_t i = 0; i < sizeof open_files / sizeof open_files[0]; i++) {
    if (open_files[i].file != NULL) continue;
    open_files[i].file = file;
    open_files[i].offset = 0;
    *handle = (intptr_t)(FIRST_FILE_HANDLE + i);
    return 0;
  }
  return EMFILE;
}

static bool open_file(intptr_t handle, size_t* index) {
  if (handle < FIRST_FILE_HANDLE) return false;
  *index = (size_t)(handle - FIRST_FILE_HANDLE);
  return *index < sizeof open_files / sizeof open_files[0] && open_files[*index].file != NULL;
}

static long io_read(void* data, intptr_t handle, uint8_t* bytes, size_t length) {
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
  if (handle != 0) return -EBADF;
  if (length == 0) return 0;
  /* One byte at a time, so the standard input port never reads ahead of the
   * line editor. ^D is end of input. */
  int c = console_read_byte();
  if (c < 0) return -EINTR;
  if (c == 0x04) return 0;
  bytes[0] = (uint8_t)(c == '\r' ? '\n' : c);
  return 1;
}

static long io_write(void* data, intptr_t handle, const uint8_t* bytes, size_t length) {
  (void)data;
  if (handle != 1 && handle != 2) return -EBADF;
  console_write((const char*)bytes, length);
  return (long)length;
}

static int io_close(void* data, intptr_t handle) {
  (void)data;
  size_t index;
  if (open_file(handle, &index)) open_files[index].file = NULL;
  return 0;
}

const ot_io_host kernel_io_host = {io_open, io_read, io_write, io_close, NULL};
