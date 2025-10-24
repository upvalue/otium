// prog-shell.cpp - TCL shell implementation
#include "ot/shared/arguments.hpp"
#include "ot/shared/messages.hpp"
#include "ot/user/tcl.h"
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

void *malloc(size_t size) {
  if (!pool) {
    oprintf("FATAL: malloc called before pool initialized (size=%d)\n", size);
    ou_exit();
  }
  void *result = tlsf_malloc(pool, size);
  if (!result && size > 0) {
    oprintf("FATAL: malloc failed - out of memory (requested=%d)\n", size);
    ou_exit();
  }
  return result;
}

void free(void *ptr) {
  if (!pool) {
    oprintf("WARNING: free called before pool initialized\n");
    return;
  }
  tlsf_free(pool, ptr);
}

void *realloc(void *ptr, size_t size) {
  if (!pool) {
    oprintf("FATAL: realloc called before pool initialized\n");
    ou_exit();
  }
  void *result = tlsf_realloc(pool, ptr, size);
  if (!result && size > 0) {
    oprintf("FATAL: realloc failed - out of memory (requested=%d)\n", size);
    ou_exit();
  }
  return result;
}

#define SHELL_PAGES 10

void shell_main() {
  // allocate some contiguous pages to work with
  memory_begin = ou_alloc_page();

  for (size_t i = 0; i != SHELL_PAGES - 1; i++) {
    ou_alloc_page();
  }

  // create memory pool
  pool = tlsf_create_with_pool(memory_begin, SHELL_PAGES * OT_PAGE_SIZE);
  if (!pool) {
    oprintf("FATAL: failed to create memory pool\n");
    ou_exit();
  }

  tcl::Interp i;
  tcl::register_core_commands(i);

  oprintf("tcl shell ready\n");

  i.register_command("quit",
                     [](tcl::Interp &i, tcl::vector<tcl::string> &argv,
                        tcl::ProcPrivdata *privdata) -> tcl::Status {
                       running = false;
                       return tcl::S_OK;
                     });

  // cause a crash by dereferencing a random addr
  i.register_command("crash",
                     [](tcl::Interp &i, tcl::vector<tcl::string> &argv,
                        tcl::ProcPrivdata *privdata) -> tcl::Status {
                       char *p = (char *)0x10;
                       (*p) = 0;
                       return tcl::S_OK;
                     });

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
