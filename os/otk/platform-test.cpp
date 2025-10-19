#include "otk/kernel.hpp"

#include <stdio.h>

// Test memory pool - large enough for tests
// We define __free_ram directly as the test memory pool
alignas(OT_PAGE_SIZE) char __free_ram[256 * OT_PAGE_SIZE];
char __free_ram_end[0]; // Zero-sized array at the end

void oputchar(char ch) { putchar(ch); }