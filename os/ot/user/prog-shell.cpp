// prog-shell.cpp - TCL shell implementation
#include "ot/lib/arguments.hpp"
#include "ot/lib/messages.hpp"
#include "ot/lib/mpack/mpack-utils.hpp"
#include "ot/user/local-storage.hpp"
#include "ot/user/tcl.hpp"
#include "ot/user/user.hpp"

#include "ot/user/prog-shell.h"

#define SHELL_PAGES 10

// Shell-specific storage inheriting from LocalStorage
struct ShellStorage : public LocalStorage {
  char buffer[OT_PAGE_SIZE];
  size_t buffer_i;
  bool running;

  ShellStorage() {
    // Initialize memory allocator
    process_storage_init(SHELL_PAGES);
    // Initialize shell state
    buffer_i = 0;
    running = true;
  }
};

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

void shell_main() {
  oprintf("SHELL BEGIN\n");

  // Get the storage page and initialize ShellStorage
  void *storage_page = ou_get_storage().as_ptr();
  ShellStorage *s = new (storage_page) ShellStorage();

  char *mp_page = (char *)ou_alloc_page();

  tcl::Interp i;
  tcl::register_core_commands(i);

  i.register_mpack_functions(mp_page, OT_PAGE_SIZE);

  oprintf("tcl shell ready\n");

  // Register quit command - uses local_storage global
  i.register_command("quit",
                     [](tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) -> tcl::Status {
                       ((ShellStorage *)local_storage)->running = false;
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

  while (s->running) {
    oprintf("> ");
    while (s->running) {
      char c = ogetchar();
      if (c >= 32 && c <= 126) {
        s->buffer[s->buffer_i++] = c;
        if (s->buffer_i == sizeof(s->buffer)) {
          oprintf("buffer full\n");
          s->buffer_i = 0;
        }
        oprintf("%c", c);
      }

      // new line
      if (c == 13) {
        s->buffer[s->buffer_i] = 0;
        oputchar('\n');
        tcl::Status status = i.eval(s->buffer);
        if (status != tcl::S_OK) {
          oprintf("tcl error: %s\n", i.result.c_str());
        } else {
          oprintf("result: %s\n", i.result.c_str());
        }
        s->buffer_i = 0;
        break;
      }

      // bksp
      if ((c == 8 || c == 127) && s->buffer_i != 0) {
        oprintf("\b \b");
        s->buffer_i--;
      }
      ou_yield();
    }
  }

  oprintf("exiting shell\n");
}
