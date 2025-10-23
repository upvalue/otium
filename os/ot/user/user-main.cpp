// user-main.cpp - Main entry point for userspace programs
#include "ot/shared/arguments.hpp"
#include "ot/shared/messages.hpp"
#include "ot/shared/mpack-reader.hpp"
#include "ot/shared/mpack-utils.hpp"
#include "ot/shared/mpack-writer.hpp"
#include "ot/user/user.hpp"
#include "ot/user/vendor/tlsf.h"

#include "ot/user/tcl.h"

// Buffer for storing user commands
static char buffer[OT_PAGE_SIZE];
static size_t buffer_i = 0;

// Whether the shell is running
static bool running = true;

void *memory_begin = nullptr;

tlsf_t pool = nullptr;

void *malloc(size_t size) { return tlsf_malloc(pool, size); }
void free(void *ptr) { tlsf_free(pool, ptr); }
void *realloc(void *ptr, size_t size) { return tlsf_realloc(pool, ptr, size); }

enum ProgramType { UNKNOWN, SHELL, SCRATCH, SCRATCH2 } program_type;

void determine_program_type() {
  program_type = UNKNOWN;
  PageAddr arg_page = ou_get_arg_page();

  MPackReader reader(arg_page.as<char>(), OT_PAGE_SIZE);

  uint32_t pair_count;
  StringView key;

  if (!reader.enter_map(pair_count)) {
    return;
  }

  bool found_args = false;

  for (size_t i = 0; i != pair_count; i++) {
    if (!reader.read_string(key)) {
      return;
    }

    if (key.equals("args")) {
      found_args = true;
      break;
    }
  }

  uint32_t argc;

  if (!found_args || !reader.enter_array(argc)) {
    return;
  }

  StringView arg;
  for (size_t i = 0; i != argc; i++) {
    if (!reader.read_string(arg))
      return;

    if (arg.equals("shell")) {
      program_type = SHELL;
      return;
    } else if (arg.equals("scratch")) {
      program_type = SCRATCH;
      return;
    } else if (arg.equals("scratch2")) {
      program_type = SCRATCH2;
      return;
    }
  }
}

void shell_main() {
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
      ou_yield();
    }
  }

  // Print memory usage report from tlsf
  oprintf("exiting shell\n");
}

void scratch_main() {
  int scratch2_pid = ou_proc_lookup("scratch2");
  oprintf("scratch2 pid: %d\n", scratch2_pid);

  // try sending to invalid pid and see what happens
  PageAddr comm_page = ou_get_comm_page();
  MPackWriter writer(comm_page.as<char>(), OT_PAGE_SIZE);
  writer.array(3).str("string").pack(1).str("hello from scratch");

  int x = ou_ipc_send_message(scratch2_pid);
  if (!x) {
    oprintf("failed to send message\n");
  }

  MPackWriter writer2(comm_page.as<char>(), OT_PAGE_SIZE);
  writer2.array(3).str("string").pack(2).str("hello from scratch");

  int y = ou_ipc_send_message(scratch2_pid);
  oprintf("scratch: sending message again\n");
  if (!y) {
    oprintf("failed to send message\n");
  }

  /*PageAddr comm_page = ou_get_comm_page();
  mpack_scratch_print(comm_page.as<char>(), OT_PAGE_SIZE);
  ou_io_puts(ot_scratch_buffer, strlen(ot_scratch_buffer));*/

  while (true) {
    int msg_count = ou_ipc_check_message();
    if (msg_count > 0) {
      oprintf("scratch: msg count: %d\n", msg_count);
      oprintf("scratch: got message, exiting\n");
      break;
    }
    ou_yield();
    break;
  }
}

void scratch2_main() {
  int messages_processed = 0;
  while (messages_processed < 2) {
    int msg_count = ou_ipc_check_message();
    if (msg_count > 0) {
      oprintf("scratch2: got message, msg count: %d\n", msg_count);
      int idx = msg_count - 1;
      PageAddr msg_page = ou_get_msg_page(idx);
      oprintf("printing message received\n");
      mpack_sprint(msg_page.as<char>(), OT_PAGE_SIZE, ot_scratch_buffer,
                   OT_PAGE_SIZE);
      ou_io_puts(ot_scratch_buffer, strlen(ot_scratch_buffer));
      ou_ipc_pop_message();
      messages_processed++;
    }
    ou_yield();
  }
  oprintf("exiting scratch2\n");
}

extern "C" void user_program_main(void) {
  determine_program_type();

  if (program_type == SHELL) {
    shell_main();
  } else if (program_type == SCRATCH) {
    scratch_main();
  } else if (program_type == SCRATCH2) {
    scratch2_main();
  } else {
    const char *str = "unknown program type, exiting\n";
    ou_io_puts(str, strlen(str));
  }

  ou_exit();
}