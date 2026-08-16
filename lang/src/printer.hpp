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

// Table iteration hook for the printer (insertion order). The weak default in
// printer.cpp reports no entries; data.cpp supplies the table-backed cursor.
bool printer_table_next(Vm& vm, Value table, u32* cursor, Value* k, Value* v);

}  // namespace ot
