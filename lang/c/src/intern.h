// intern.h — string interning: open-addressed hash of name -> u32 id,
// ids dense from 1. Names are copied into ot_alloc'd storage owned by Intern.
#pragma once
#include "common.h"
#include "vec.h"

typedef struct InternName {
  char* s;
  u32 len;
} InternName;
OT_VEC_TYPE(InternName, VecInternName);

typedef struct Intern {
  VecInternName names;  // id - 1 -> name
  u32* slots;           // open-addressed table of ids (0 = empty)
  u32 slotCap;          // power of two
} Intern;

void intern_init(Intern* in);
void intern_deinit(Intern* in);
u32 intern_id(Intern* in, const char* s, u32 len);       // idempotent
const char* intern_name(Intern* in, u32 id, u32* lenOut);  // null if id invalid
