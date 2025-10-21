#include "ot/shared/address.hpp"
#include "ot/shared/arguments.hpp"
#include "ot/user/user.hpp"

void ou_get_arguments(Arguments &args) {
  PageAddr arg_page = ou_get_arg_page();
  if (arg_page.is_null()) {
    args.argc = 0;
    args.argv = nullptr;
    return;
  }
  uintptr_t *arg_ptr = arg_page.as<uintptr_t>();
  args.argc = *arg_ptr;
  arg_ptr++;
  args.argv = (char **)arg_ptr;
}