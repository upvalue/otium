#ifndef OT_USER_HPP
#define OT_USER_HPP

#include "ot/common.h"

#include "ot/shared/address.hpp"
#include "ot/shared/arguments.hpp"

// system calls
extern "C" {
void oputchar(char ch);
int ogetchar();
void ou_yield(void);
void ou_exit(void);
void *ou_alloc_page(void);
}

PageAddr ou_get_arg_page(void);

/**
 * Sets up arguments passed to the process or a nullptr if no
 * arguments were given
 */
void ou_get_arguments(Arguments &args);

#endif