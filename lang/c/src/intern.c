#include "intern.h"

static u32 fnv1a(const char* s, u32 len) {
  u32 h = 2166136261u;
  for (u32 i = 0; i < len; i++) {
    h ^= (u8)s[i];
    h *= 16777619u;
  }
  return h;
}

static void intern_grow(Intern* in) {
  u32 ncap = in->slotCap ? in->slotCap * 2 : 64;
  u32* ns = (u32*)ot_alloc((size_t)ncap * sizeof(u32));
  if (!ns) ot_fatal("intern: out of memory");
  memset(ns, 0, (size_t)ncap * sizeof(u32));
  for (u32 i = 0; i < in->slotCap; i++) {
    u32 id = in->slots[i];
    if (!id) continue;
    InternName* n = &in->names.data[id - 1];
    u32 j = fnv1a(n->s, n->len) & (ncap - 1);
    while (ns[j]) j = (j + 1) & (ncap - 1);
    ns[j] = id;
  }
  ot_free(in->slots);
  in->slots = ns;
  in->slotCap = ncap;
}

void intern_init(Intern* in) {
  memset(&in->names, 0, sizeof(in->names));
  in->slots = nullptr;
  in->slotCap = 0;
  intern_grow(in);
}

void intern_deinit(Intern* in) {
  for (u32 i = 0; i < in->names.len; i++) ot_free(in->names.data[i].s);
  vec_deinit(&in->names);
  ot_free(in->slots);
}

u32 intern_id(Intern* in, const char* s, u32 len) {
  if (in->names.len + 1 > in->slotCap * 7 / 10) intern_grow(in);
  u32 j = fnv1a(s, len) & (in->slotCap - 1);
  while (in->slots[j]) {
    InternName* n = &in->names.data[in->slots[j] - 1];
    if (n->len == len && memcmp(n->s, s, len) == 0) return in->slots[j];
    j = (j + 1) & (in->slotCap - 1);
  }
  char* copy = (char*)ot_alloc((size_t)len + 1);
  if (!copy) ot_fatal("intern: out of memory");
  memcpy(copy, s, len);
  copy[len] = 0;
  vec_push(&in->names, ((InternName){copy, len}));
  u32 id = in->names.len;  // dense from 1
  in->slots[j] = id;
  return id;
}

const char* intern_name(Intern* in, u32 id, u32* lenOut) {
  if (id == 0 || id > in->names.len) {
    if (lenOut) *lenOut = 0;
    return nullptr;
  }
  InternName* n = &in->names.data[id - 1];
  if (lenOut) *lenOut = n->len;
  return n->s;
}
