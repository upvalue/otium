#ifndef OTOS_KERNEL_H
#define OTOS_KERNEL_H

#include <stddef.h>
#include <stdint.h>

void console_write(const char* bytes, size_t length);
int console_read_byte(void);
/* Reads what has arrived without waiting; true if it included ^C. */
bool console_poll_interrupt(void);

[[noreturn]] void sbi_shutdown(bool failure);

#endif
