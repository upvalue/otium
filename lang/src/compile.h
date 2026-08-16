// compile.h - expanded Otium forms to bytecode.
#pragma once
#include "slots.h"

typedef struct State State;

// Compile one already-expanded form as a zero-argument top-level thunk.
// The returned Code is executable with ot_execute_code.
Value compile_form_ref(State* vm, Ref dst, Ref expanded);
