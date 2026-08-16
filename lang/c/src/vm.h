// vm.h - the bytecode interpreter.
#pragma once
#include "value.h"

typedef struct State State;

// Create a compiled Function/Macro. captures is an Array of boxed values.
Value make_compiled_function(State* vm, Value code, Value captures, Value nsName, u32 name,
                             bool macro);

// Call a value whose arguments already occupy stack[base..base+argc). Compiled
// calls execute only until their frame floor, so this is safe when a native
// callback re-enters an already-running machine.
Value vm_call(State* vm, Value callee, u32 base, u32 argc);

// Execute frames until the frame stack returns to floor.
Value vm_execute(State* vm, u32 floor);

// Execute a hand-assembled code object as a zero-argument top-level thunk.
Value vm_execute_code(State* vm, Value code);
