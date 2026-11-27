#ifndef OTOS_KERNEL_H
#define OTOS_KERNEL_H

#include "otium.h"

#include <stddef.h>
#include <stdint.h>

/* What a board file supplies: mps2.c for QEMU. The board's reset code sets
 * up the console and clock and then calls kmain. */
[[noreturn]] void kmain(void);

void console_write(const char* bytes, size_t length);
/* Waits for a byte. Returns -1 instead if ^C interrupts Otium code. */
int console_read_byte(void);

/* Set by ^C while console_evaluating is true; the runtime's interrupt flag. */
extern volatile sig_atomic_t console_interrupt;
extern volatile bool console_evaluating;

/* Nanoseconds since boot. */
uint64_t kernel_uptime_ns(void);

/* Resets the board. Under QEMU with -no-reboot this ends the emulator. */
[[noreturn]] void board_reset(bool failure);

/* The console on handles 0, 1 and 2 and the embedded files, read-only. */
extern const ot_io_host kernel_io_host;

#endif
