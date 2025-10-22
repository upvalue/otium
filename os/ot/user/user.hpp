#ifndef OT_USER_HPP
#define OT_USER_HPP

#include "ot/common.h"

#include "ot/shared/address.hpp"
#include "ot/shared/arguments.hpp"

// system calls
extern "C" {
int oputchar(char ch);
int ogetchar();
void ou_yield(void);
void ou_exit(void);
void *ou_alloc_page(void);
}

PageAddr ou_get_arg_page(void);
int ou_io_puts(const char *str, int size);
int ou_proc_lookup(const char *name);

/**
 * Sets up arguments passed to the process or a nullptr if no
 * arguments were given
 */
void ou_get_arguments(Arguments &args);

#endif