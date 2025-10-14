#include "otk/kernel.hpp"

#include <stdio.h>

// Dummy RAM region for testing (16KB)
char free_ram;

extern "C" char *__free_ram = &free_ram, *__free_ram_end = &free_ram;

void oputchar(char ch) { putchar(ch); }