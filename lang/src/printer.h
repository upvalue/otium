#pragma once
#include "common.h"
#include "slots.h"
#include "value.h"
#include "vec.h"

typedef struct State State;

// Spec 2.6. repr reads back where possible; display is identical except
// strings and buffers render as raw characters, recursively in collections.
void print_ref_repr(State* vm, Ref v, Buf* out);
void print_ref_display(State* vm, Ref v, Buf* out);
