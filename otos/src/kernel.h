#ifndef OTOS_KERNEL_H
#define OTOS_KERNEL_H

#include <stddef.h>
#include <stdint.h>

void console_write(const char* bytes, size_t length);
int console_read_byte(void);

[[noreturn]] void sbi_shutdown(bool failure);

#endif
