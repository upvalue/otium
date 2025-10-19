#ifndef OT_USER_HPP
#define OT_USER_HPP

#include "ot/common.h"

// system calls
extern "C" {

// Terminal controls
void oputchar(char ch);
int ogetchar();

void ou_yield(void);
void ou_exit(void);
void *ou_alloc_page(void);
}
#endif