// prog-echo.cpp - Simple echo program that prints its arguments
#include "ot/lib/mpack/mpack-reader.hpp"
#include "ot/user/prog.h"
#include "ot/user/user.hpp"

void echo_main() {
  oprintf("ECHO: starting\n");
  PageAddr arg_page = ou_get_arg_page();

  MPackReader reader(arg_page.as<char>(), OT_PAGE_SIZE);

  // Read the args from the arg page: {"args": ["echo", "arg1", "arg2", ...]}
  constexpr size_t MAX_ARGS = 32;
  StringView argv[MAX_ARGS];
  size_t argc = 0;

  if (!reader.read_args_map(argv, MAX_ARGS, argc)) {
    oprintf("echo: failed to read arguments\n");
    return;
  }

  // Print each argument (skip argv[0] which is the program name)
  for (size_t i = 1; i < argc; i++) {
    if (i > 1) {
      oputchar(' ');
    }
    oputsn(argv[i].ptr, argv[i].len);
  }
  oputchar('\n');
}
