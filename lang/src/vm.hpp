// vm.hpp - the bytecode interpreter.
#pragma once
#include "value.hpp"

namespace ot {

struct State;

// Create a compiled Function/Macro. captures is an Array of boxed values.
Value make_compiled_function(State&, Value code, Value captures, Value nsName, u32 name,
                             bool macro = false);

// Call a value whose arguments already occupy stack[base..base+argc). Compiled
// calls execute only until their frame floor, so this is safe when a native
// callback re-enters an already-running machine.
Value vm_call(State&, Value callee, u32 base, u32 argc);

// Execute frames until the frame stack returns to floor.
Value vm_execute(State&, u32 floor);

// Execute a hand-assembled code object as a zero-argument top-level thunk.
Value vm_execute_code(State&, Value code);

}  // namespace ot
