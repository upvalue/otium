#pragma once
#include "ot/common.h"

// Auto-generated method IDs
namespace MethodIds {
  namespace Fibonacci {
    constexpr intptr_t CALC_FIB = 0x1000;
    constexpr intptr_t CALC_PAIR = 0x1100;
    constexpr intptr_t GET_CACHE_SIZE = 0x1200;
  }
  namespace Graphics {
    constexpr intptr_t GET_FRAMEBUFFER = 0x1300;
    constexpr intptr_t FLUSH = 0x1400;
    constexpr intptr_t REGISTER_APP = 0x1500;
    constexpr intptr_t SHOULD_RENDER = 0x1600;
    constexpr intptr_t UNREGISTER_APP = 0x1700;
  }
  namespace Filesystem {
    constexpr intptr_t OPEN = 0x1800;
    constexpr intptr_t READ = 0x1900;
    constexpr intptr_t WRITE = 0x1a00;
    constexpr intptr_t CLOSE = 0x1b00;
    constexpr intptr_t CREATE_FILE = 0x1c00;
    constexpr intptr_t CREATE_DIR = 0x1d00;
    constexpr intptr_t DELETE_FILE = 0x1e00;
    constexpr intptr_t DELETE_DIR = 0x1f00;
    constexpr intptr_t LIST_DIR = 0x2000;
  }
  namespace Keyboard {
    constexpr intptr_t POLL_KEY = 0x2100;
  }
}
