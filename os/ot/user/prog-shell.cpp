// prog-shell.cpp - shell program
#include "ot/shared/arguments.hpp"
#include "ot/shared/mpack-reader.hpp"
#include "ot/shared/mpack-utils.hpp"
#include "ot/user/user.hpp"
#include "ot/user/vendor/tlsf.h"

#include "ot/user/tcl.h"

// Buffer for storing user commands
static char buffer[4096];
static size_t buffer_i = 0;

// Whether the shell is running
static bool running = true;

void *memory_begin = nullptr;

tlsf_t pool = nullptr;

void *malloc(size_t size) { return tlsf_malloc(pool, size); }
void free(void *ptr) { tlsf_free(pool, ptr); }
void *realloc(void *ptr, size_t size) { return tlsf_realloc(pool, ptr, size); }

#ifdef OT_ARCH_WASM
extern "C" void shell_main(void) {
#else
extern "C" void main(void) {
#endif
  PageAddr arg_page = ou_get_arg_page();

  int x = mpack_print(arg_page.as<char>(), OT_PAGE_SIZE, oputchar);
  oprintf("mpack_print returned %d\n", x);
  // MPackReader reader(arg_page.as<char>(), OT_PAGE_SIZE);
  /*Arguments args;
  ou_get_arguments(args);

  if (args.argc > 0) {
    // oprintf("arg page ptr %x\n", (uintptr_t)args.arg_page.as_ptr());
    oprintf("I got me some arguments! :)  %d\n", args.argc);
    for (size_t i = 0; i != args.argc; i++) {
      oprintf("argument ptr %x\n", (uintptr_t)args.argv[i]);
      oprintf("Argument %d: %s\n", i, args.argv[i]);
    }
  }*/

  // allocate some contiguous pages to work with
  memory_begin = ou_alloc_page();

  for (size_t i = 0; i != 9; i++) {
    ou_alloc_page();
  }

  // create memory pool
  pool = tlsf_create_with_pool(memory_begin, 10 * OT_PAGE_SIZE);

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
    }
  }

  // Print memory usage report from tlsf
  oprintf("exiting shell\n");

  // TODO: This crashes, why?
  // oprintf("memory usage: %zu bytes\n", tlsf_block_size(pool));

  ou_exit();
}