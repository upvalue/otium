// user-main.cpp - Main entry point for userspace programs
#include "ot/lib/arguments.hpp"
#include "ot/lib/messages.hpp"
#include "ot/lib/mpack/mpack-reader.hpp"
#include "ot/lib/mpack/mpack-utils.hpp"
#include "ot/lib/mpack/mpack-writer.hpp"
#include "ot/user/prog-shell.h"
#include "ot/user/user.hpp"
#include "ot/vendor/tlsf/tlsf.h"

enum ProgramType { UNKNOWN, SHELL } program_type;

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
    }
  }
}

extern "C" void user_program_main(void) {
  determine_program_type();

  if (program_type == SHELL) {
    shell_main();
  } else {
    const char *str = "unknown program type, exiting\n";
    ou_io_puts(str, strlen(str));
  }

  ou_exit();
}