// intern.hpp — string interning: open-addressed hash of name -> u32 id,
// ids dense from 1. Names are copied into malloc'd storage owned by Intern.
#pragma once
#include "common.hpp"
#include "vec.hpp"

namespace ot {

struct Intern {
  struct Name { char* s; u32 len; };

  Intern();
  ~Intern();
  Intern(const Intern&) = delete;
  Intern& operator=(const Intern&) = delete;

  u32 intern(const char* s, u32 len);      // idempotent
  const char* name(u32 id, u32* lenOut);   // null if id invalid

  // internals
  Vec<Name> names;   // id - 1 -> name
  u32* slots;        // open-addressed table of ids (0 = empty)
  u32 slotCap;       // power of two
  void grow();
};

} // namespace ot
