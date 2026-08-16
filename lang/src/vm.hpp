// vm.hpp - the bytecode interpreter.
#pragma once
#include "value.hpp"

namespace ot {

struct State;

// Execute a hand-assembled code object as a zero-argument top-level thunk.
// This narrow entry point grows into the re-entrant call API once call frames
// land; keeping it now makes the dispatch loop independently testable.
Value vm_execute_code(State&, Value code);

}  // namespace ot
