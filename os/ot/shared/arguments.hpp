#ifndef OT_SHARED_ARGUMENTS_HPP
#define OT_SHARED_ARGUMENTS_HPP

#include "ot/common.h"

struct Arguments {
  uintptr_t argc;
  char **argv;
};

#endif