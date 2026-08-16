#pragma once
#include "common.hpp"
#include "value.hpp"
#include "vec.hpp"

namespace ot {

struct State;

// Spec 2.6. repr reads back where possible; display is identical except
// strings and buffers render as raw characters, recursively in collections.
void print_repr(State& vm, Value v, Buf& out);
void print_display(State& vm, Value v, Buf& out);

}  // namespace ot
