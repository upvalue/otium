#ifndef OT_USER_HPP
#define OT_USER_HPP

#include "ot/common.h"

#include "ot/shared/address.hpp"
#include "ot/shared/arguments.hpp"
#include "ot/shared/mpack-writer.hpp"

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
int ou_ipc_check_message(void);

/** Send the message in the comm page to a given PID */
int ou_ipc_send_message(int pid);

/**
 * Sets up arguments passed to the process or a nullptr if no
 * arguments were given
 */
void ou_get_arguments(Arguments &args);

/** Convenience struct for writing to the comm page */
struct CommWriter {
  PageAddr comm_page;
  MPackWriter _writer;

  CommWriter();
  MPackWriter &writer() { return _writer; }
};

#endif