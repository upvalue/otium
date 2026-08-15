#include "intern.hpp"

namespace ot {

static u32 fnv1a(const char* s, u32 len) {
  u32 h = 2166136261u;
  for (u32 i = 0; i < len; i++) { h ^= (u8)s[i]; h *= 16777619u; }
  return h;
}

Intern::Intern() : slots(nullptr), slotCap(0) { grow(); }

Intern::~Intern() {
  for (u32 i = 0; i < names.len; i++) free(names.data[i].s);
  free(slots);
}

void Intern::grow() {
  u32 ncap = slotCap ? slotCap * 2 : 64;
  u32* ns = (u32*)calloc(ncap, sizeof(u32));
  if (!ns) ot_fatal("intern: out of memory");
  for (u32 i = 0; i < slotCap; i++) {
    u32 id = slots[i];
    if (!id) continue;
    Name& n = names.data[id - 1];
    u32 j = fnv1a(n.s, n.len) & (ncap - 1);
    while (ns[j]) j = (j + 1) & (ncap - 1);
    ns[j] = id;
  }
  free(slots);
  slots = ns; slotCap = ncap;
}

u32 Intern::intern(const char* s, u32 len) {
  if (names.len + 1 > slotCap * 7 / 10) grow();
  u32 j = fnv1a(s, len) & (slotCap - 1);
  while (slots[j]) {
    Name& n = names.data[slots[j] - 1];
    if (n.len == len && memcmp(n.s, s, len) == 0) return slots[j];
    j = (j + 1) & (slotCap - 1);
  }
  char* copy = (char*)malloc((size_t)len + 1);
  if (!copy) ot_fatal("intern: out of memory");
  memcpy(copy, s, len);
  copy[len] = 0;
  names.push(Name{copy, len});
  u32 id = names.len;  // dense from 1
  slots[j] = id;
  return id;
}

const char* Intern::name(u32 id, u32* lenOut) {
  if (id == 0 || id > names.len) { if (lenOut) *lenOut = 0; return nullptr; }
  Name& n = names.data[id - 1];
  if (lenOut) *lenOut = n.len;
  return n.s;
}

} // namespace ot
