#include "ot/common.h"

char *__kernel_base = nullptr;
char *user_entry = nullptr;

// Stub for user_program_main (required by process.cpp)
void user_program_main(void) {}
