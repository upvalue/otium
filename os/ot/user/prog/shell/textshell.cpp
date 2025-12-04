// textshell.cpp - Text-based TCL shell implementation
#include <stdio.h>

#include "ot/lib/mpack/mpack-utils.hpp"
#include "ot/user/gen/tcl-vars.hpp"
#include "ot/user/local-storage.hpp"
#include "ot/user/prog.h"
#include "ot/user/prog/shell/commands.hpp"
#include "ot/user/tcl.hpp"
#include "ot/user/user.hpp"

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

void shell_main() {
  oprintf("SHELL BEGIN\n");

  // Get the storage page and initialize ShellStorage
  void *storage_page = ou_get_storage().as_ptr();
  ShellStorage *s = new (storage_page) ShellStorage();

  char *mp_page = (char *)ou_alloc_page();

  tcl::Interp i;
  tcl::register_core_commands(i);

  i.register_mpack_functions(mp_page, OT_PAGE_SIZE);

  // Register IPC method ID variables
  register_ipc_method_vars(i);

  // Execute shellrc startup script
#include "shellrc.hpp"

  oprintf("tcl shell ready\n");

  // Register shared shell commands (proc/lookup, ipc/send, error/string, fs/*)
  shell::register_shell_commands(i);

  // Register text shell-specific commands
  i.register_command(
      "quit",
      [](tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) -> tcl::Status {
        ((ShellStorage *)local_storage)->running = false;
        return tcl::S_OK;
      },
      nullptr, "[quit] - Quit the shell");

  i.register_command(
      "shutdown",
      [](tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) -> tcl::Status {
        ou_shutdown();
        return tcl::S_OK; // Never reached
      },
      nullptr, "[shutdown] - Shutdown all processes and exit the kernel");

  i.register_command(
      "crash",
      [](tcl::Interp &i, tcl::vector<tcl::string> &argv, tcl::ProcPrivdata *privdata) -> tcl::Status {
        char *p = (char *)0x10;
        (*p) = 0;
        return tcl::S_OK;
      },
      nullptr, "[crash] - Cause a crash");

  // load shellrc
  tcl::Status shellrc_status = i.eval(tcl::string_view(shellrc_content));
  if (shellrc_status != tcl::S_OK) {
    oprintf("shellrc error: %s\n", i.result.c_str());
  }

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
