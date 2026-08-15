#pragma once
#include "common.hpp"
#include "value.hpp"
#include "vec.hpp"

namespace ot {

struct Vm;

// Spec 2.6. repr reads back where possible; display is identical except
// strings and buffers render as raw characters, recursively in collections.
void print_repr(Vm& vm, Value v, Buf& out);
void print_display(Vm& vm, Value v, Buf& out);

// Table iteration hooks for the printer (insertion order). Weak defaults in
// printer.cpp report zero entries; the integrator supplies strong definitions
// backed by the table_iter machinery from heap.hpp/builtins/data.cpp.
u32 printer_table_count(Vm& vm, Value table);
bool printer_table_entry(Vm& vm, Value table, u32 i, Value* k, Value* v);

}  // namespace ot
