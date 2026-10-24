#ifndef OTOS_KERNEL_H
#define OTOS_KERNEL_H

#include <stddef.h>
#include <stdint.h>

void console_write(const char* bytes, size_t length);
int console_read_byte(void);
/* Reads what has arrived without waiting; true if it included ^C. */
bool console_poll_interrupt(void);

/* Registers the otos.gfx module with the state. */
struct ot_state;
void gfx_register(struct ot_state* state);

[[noreturn]] void sbi_shutdown(bool failure);

#endif
