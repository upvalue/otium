#ifndef _USER_HPP
#define _USER_HPP

#include "otcommon.h"

// system calls
extern "C" {
void ou_putchar(char ch);
void ou_yield(void);
__attribute__((noreturn)) void ou_exit(void);
}
#endif