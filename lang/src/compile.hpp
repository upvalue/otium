// compile.hpp - expanded Otium forms to bytecode.
#pragma once
#include "value.hpp"

namespace ot {

struct State;

// Compile one already-expanded form as a zero-argument top-level thunk.
// The returned Code is executable with vm_execute_code.
Value compile_form(State&, Value expanded);

}  // namespace ot
