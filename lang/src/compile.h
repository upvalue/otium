// compile.h - expanded Otium forms to bytecode.
#pragma once
#include "value.h"

typedef struct State State;

// Compile one already-expanded form as a zero-argument top-level thunk.
// The returned Code is executable with vm_execute_code.
Value compile_form(State* vm, Value expanded);
