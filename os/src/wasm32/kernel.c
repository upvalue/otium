#include "../common.h"

extern void host_putchar(char c);

void putchar(char c) { host_putchar(c); }

void kernel_enter() { printf("aw shucks\n"); }