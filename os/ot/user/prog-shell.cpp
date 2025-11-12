// prog-shell.cpp - TCL shell implementation
#include "ot/shared/arguments.hpp"
#include "ot/shared/messages.hpp"
#include "ot/shared/mpack-utils.hpp"
#include "ot/user/tcl.hpp"
#include "ot/user/user.hpp"
#include "ot/user/vendor/tlsf.h"

#include "ot/user/prog-shell.h"

// Buffer for storing user commands
static char buffer[OT_PAGE_SIZE];
static size_t buffer_i = 0;

// Whether the shell is running
static bool running = true;

void *memory_begin = nullptr;
tlsf_t pool = nullptr;

void *ou_malloc(size_t size) {
  if (!pool) {
    oprintf("FATAL: ou_malloc called before pool initialized (size=%d)\n", size);
    ou_exit();
  }
  void *result = tlsf_malloc(pool, size);
  if (!result && size > 0) {
    oprintf("FATAL: ou_malloc failed - out of memory (requested=%d)\n", size);
    ou_exit();
  }
  return result;
}

void ou_free(void *ptr) {
  if (!pool) {
    oprintf("WARNING: ou_free called before pool initialized\n");
    return;
  }
  tlsf_free(pool, ptr);
}

void *ou_realloc(void *ptr, size_t size) {
  if (!pool) {
    oprintf("FATAL: ou_realloc called before pool initialized\n");
    ou_exit();
  }
  void *result = tlsf_realloc(pool, ptr, size);
  if (!result && size > 0) {
    oprintf("FATAL: ou_realloc failed - out of memory (requested=%d)\n", size);
    ou_exit();
  }
  return result;
}

#define SHELL_PAGES 10

tcl::Status cmd_proc_lookup(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("proc/lookup", argv, 2, 2)) {
    osnprintf(ot_scratch_buffer, OT_PAGE_SIZE, "arity check failed");
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }
  int proc_pid = ou_proc_lookup(argv[1].c_str());
  if (proc_pid == 0) {
    osnprintf(ot_scratch_buffer, OT_PAGE_SIZE, "proc not found");
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }
  char buf[32];
  osnprintf(buf, sizeof(buf), "%d", proc_pid);
  i.result = buf;
  return tcl::S_OK;
}

tcl::Status cmd_mp_send(tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) {
  if (!i.arity_check("mp/send", argv, 2, 2)) {
    return tcl::S_ERR;
  }
  int proc_pid = atoi(argv[1].c_str());
  if (proc_pid == 0) {
    osnprintf(ot_scratch_buffer, OT_PAGE_SIZE, "could not convert %s to int", argv[1].c_str());
    i.result = ot_scratch_buffer;
    return tcl::S_ERR;
  }
  PageAddr comm_page = ou_get_comm_page();
  memcpy(comm_page.as<char>(), i.mpack_buffer_, i.mpack_buffer_size_);
  if (!ou_ipc_send_message(proc_pid)) {
    mpack_oprint(comm_page.as<char>(), OT_PAGE_SIZE);
    oputchar('\n');
    return tcl::S_ERR;
  }
  return tcl::S_OK;
}

void shell_main() {
  oprintf("SHELL BEGIN\n");
  // allocate some contiguous pages to work with
  memory_begin = ou_alloc_page();

  for (size_t i = 0; i != SHELL_PAGES - 1; i++) {
    ou_alloc_page();
  }

  char *mp_page = (char *)ou_alloc_page();

  // create memory pool
  pool = tlsf_create_with_pool(memory_begin, SHELL_PAGES * OT_PAGE_SIZE);
  if (!pool) {
    oprintf("FATAL: failed to create memory pool\n");
    ou_exit();
  }

  tcl::Interp i;
  tcl::register_core_commands(i);

  i.register_mpack_functions(mp_page, OT_PAGE_SIZE);

  oprintf("tcl shell ready\n");

  i.register_command("quit",
                     [](tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) -> tcl::Status {
                       running = false;
                       return tcl::S_OK;
                     });

  // cause a crash by dereferencing a random addr
  i.register_command("crash",
                     [](tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) -> tcl::Status {
                       char *p = (char *)0x10;
                       (*p) = 0;
                       return tcl::S_OK;
                     });

  // Lookup a procedure's PID
  i.register_command("proc/lookup", cmd_proc_lookup, nullptr,
                     "[proc/lookup name:string] => pid:int - Lookup a procedure's PID");

  i.register_command("mp/send", cmd_mp_send, nullptr,
                     "[mp/send pid:int] => nil - Send MessagePack buffer to "
                     "the specified process");

  while (running) {
    oprintf("> ");
    while (running) {
      char c = ogetchar();
      if (c >= 32 && c <= 126) {
        buffer[buffer_i++] = c;
        if (buffer_i == sizeof(buffer)) {
          oprintf("buffer full\n");
          buffer_i = 0;
        }
        oprintf("%c", c);
      }

      // new line
      if (c == 13) {
        buffer[buffer_i] = 0;
        oputchar('\n');
        tcl::Status s = i.eval(buffer);
        if (s != tcl::S_OK) {
          oprintf("tcl error: %s\n", i.result.c_str());
        } else {
          oprintf("result: %s\n", i.result.c_str());
        }
        buffer_i = 0;
        break;
      }

      // bksp
      if ((c == 8 || c == 127) && buffer_i != 0) {
        oprintf("\b \b");
        buffer_i--;
      }
      ou_yield();
    }
  }

  // Print memory usage report from tlsf
  oprintf("exiting shell\n");
}
