#ifndef _USER_HPP
#define _USER_HPP

#include "otcommon.h"

// system calls
extern "C" {
void oputchar(char ch);
int ogetchar();
void ou_yield(void);
void ou_exit(void);
void *ou_alloc_page(void);
}
#endif