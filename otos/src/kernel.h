#ifndef OTOS_KERNEL_H
#define OTOS_KERNEL_H

#include "otium.h"

#include <stddef.h>
#include <stdint.h>

void console_write(const char* bytes, size_t length);
int console_read_byte(void);

/* Nanoseconds since boot. */
uint64_t kernel_uptime_ns(void);

/* The console on handles 0, 1 and 2 and the embedded files, read-only. */
extern const ot_io_host kernel_io_host;

[[noreturn]] void sbi_shutdown(bool failure);

#endif
