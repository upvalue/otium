#ifndef OT_USER_HPP
#define OT_USER_HPP

#include "ot/common.h"

#include "ot/lib/address.hpp"
#include "ot/lib/arguments.hpp"
#include "ot/lib/ipc.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"

// system calls
// Note: oputchar/ogetchar are also declared in common.h with C linkage
extern "C" {
int oputchar(char ch);
int ogetchar();
}

void ou_yield(void);
void ou_exit(void);
void *ou_alloc_page(void);
IpcResponse ou_ipc_send(int pid, uintptr_t flags, intptr_t method, intptr_t arg0, intptr_t arg1, intptr_t arg2);
IpcMessage ou_ipc_recv(void);
void ou_ipc_reply(IpcResponse response);

PageAddr ou_get_arg_page(void);
PageAddr ou_get_comm_page(void);
PageAddr ou_get_storage(void);
int ou_io_puts(const char *str, int size);

int ou_proc_lookup(const char *name);

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

#define OT_MAX(a, b) ((a) > (b) ? (a) : (b))
#define OT_MIN(a, b) ((a) < (b) ? (a) : (b))

#endif