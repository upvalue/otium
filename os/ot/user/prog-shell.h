// prog-shell.h - TCL shell interface
#ifndef OT_USER_PROG_SHELL_H
#define OT_USER_PROG_SHELL_H

#include "ot/vendor/tlsf/tlsf.h"

extern void *memory_begin;
extern tlsf_t pool;

void shell_main();

#endif