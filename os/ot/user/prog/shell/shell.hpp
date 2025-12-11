// shell.hpp - Shared shell storage base class
#pragma once

#include "ot/user/local-storage.hpp"

namespace shell {

// Base shell storage with common state shared by text and UI shells
struct ShellStorage : public LocalStorage {
  bool running;

  ShellStorage() : running(true) {}
};

} // namespace shell
