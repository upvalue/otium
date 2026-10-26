# Register VM design

This replaces the prototype's stack VM (`prototype/vm.c`) and retargets the
simple compiler (`prototype/compiler.c`) to it. It follows the register VM
sketch in `compiler-plan.md`, limited to what a non-optimizing, single-pass
compiler can use. The goal is to beat interpreters that do little optimization,
such as CPython and Lua, on calls, arithmetic and loops.

Out of scope for now: inline caches for slots and generic functions, per-
safepoint slot maps, raw (unboxed) float registers, and the optimizing compiler.
The design leaves room for each.

## Values

Unchanged. Fixnums have the low bit set, heap pointers are 8-byte aligned, and
`nil`, booleans, the empty list, characters and builtins are small immediates.

## Instructions

Each instruction is 8 bytes:

```c
typedef struct {
  uint8_t op, x;
  uint16_t a, b, c;
} Instruction;
```

`a`, `b` and `c` name registers or hold small operands. `b` and `c` together
form a 32-bit field (`bc`) for constant indexes, global indexes, function
indexes, jump targets and 32-bit immediates. `x` is a spare byte for flags.
Instructions that need a register pair and a jump target (fused compare and
branch, restart scopes) use a second word whose `bc` holds the target.

Jump targets are absolute instruction indexes. Functions are limited to 65536
registers and calls to 65535 arguments.

## Frames and registers

All frames share one value stack. Register `r` of a frame is
`stack[base + r]`. Register 0 holds the running closure, parameters start at
register 1, and the remaining registers hold locals and temporaries. A frame
needs `1 + locals` slots.

Frame records stay in a separate array:

```c
typedef struct {
  uint32_t function;
  size_t ip, base, floor;
} Frame;
```

`floor` is the stack height when the frame was entered. A call places the callee
in register `R` of the caller with its arguments in `R+1 .. R+n`. The callee's
frame starts at `base + R`, so arguments are already in place.

The result of a call is written to the callee slot, `stack[callee base]`, which
is register `R` in the caller.

## GC rooting

The VM stack is one root range, `stack[0 .. sp)`. The moving collector updates
those slots in place, so the dispatch loop keeps a pointer to the current
frame's registers and only reloads it when the stack array may have been
reallocated (after calls out to C).

Invariant: while a frame runs, `sp >= max(floor, base + 1 + locals)`, the
frame's top. `sp` is never lowered below the top of a frame that will resume.
Every slot in `[0, sp)` holds a valid value.

- Entering a frame raises `sp` to `max(floor, new top)`. Slots between the old
  `sp` and the new top are set to unbound, because a slot above `sp` may hold a
  pointer that a collection has since made stale.
- Returning sets `sp` back to the popped frame's floor, which is the caller's
  height at the call.
- A tail call keeps the floor and recomputes the top.
- Catching a condition, resuming a restart and running a cleanup restore the
  `sp` saved when the handler was installed, which is that frame's top. A
  caught condition is written to the register named by `TRY`. A restart's
  result is left in the slot just above the frame top, and `RESULT` copies it
  into a register.
- C code that pushes scratch values above `sp` writes them before raising `sp`,
  as it does today.

The cost is that dead registers inside a live frame stay reachable until they
are overwritten. To match the stack VM, the compiler clears `let` bindings when
a non-tail scope ends. Precise liveness waits for slot maps.

## Dispatch

One function, `dispatch`, runs instructions until the frame count returns to
the caller's stop depth. It keeps `pc`, the register pointer, the current
function and the constants pointer in locals, and reloads them from the frame
record only after calls, returns, errors and calls out to C.

Each handler is written once with `CASE(op)` and `NEXT()` macros. With GCC or
Clang they expand to computed goto (a label table indexed by opcode, one
indirect jump per handler). Defining `OTIUM_VM_SWITCH` builds a portable
`switch` loop instead; `make test-switch` builds and runs the C tests that way. A
tail-call (`musttail`) version can be added later from the same handlers.

The interrupt flag is checked at function entry and on backward jumps, not on
every instruction. Errors exit `dispatch` and are handled by the existing
unwinding loop (`rt_drive`), which finds try frames, cleanups and restarts as
before and then re-enters `dispatch`.

## Instruction set

Registers are written `rA`; `K[i]` is the runtime constant table and `G[i]`
the global table.

Moves and data:

| op | effect |
| --- | --- |
| `MOVE a b` | `rA = rB` |
| `IMM a bc` | `rA` = 32-bit immediate, sign extended (small fixnums, characters, nil, booleans, empty list, unbound) |
| `CONST a bc` | `rA = K[bc]` |
| `GLOBAL a bc` | `rA = G[bc]`, error if unbound |
| `SET_GLOBAL a bc` | `G[bc] = rA`, error if unbound |
| `DEFINE_GLOBAL a bc` | `G[bc] = rA` |
| `UPVALUE a b` | `rA` = closure field `b` |
| `UPVALUE_BOX a b` | `rA` = contents of the box in closure field `b`, error if unbound |
| `SET_UPVALUE a b` | store `rA` into the box in closure field `b` |
| `BOX a` | `rA` = new box holding `rA` |
| `UNBOX a b` | `rA` = contents of box `rB`, error if unbound |
| `SET_BOX a b` | store `rB` into box `rA` |
| `CHECK a` | error if `rA` is unbound |
| `CLEAR a b` | set `rA .. rA+b-1` to unbound |
| `CLOSURE a bc` | `rA` = new closure of function `bc` |

Control:

| op | effect |
| --- | --- |
| `JUMP bc` | jump; a backward jump checks for interrupts |
| `JFALSE a bc`, `JTRUE a bc` | jump on `rA`'s truth |
| `JBOUND a bc` | jump if `rA` is not unbound (optional parameters) |
| `CALL a b` | call `rA` with `rA+1 .. rA+b`, result in `rA` |
| `TAIL_CALL a b` | tail call |
| `RETURN a` | return `rA` |

Primitive operations write `rA` from `rB` and `rC`:

`ADD SUB MUL LT LE GT GE NUMEQ EQ CONS CAR CDR NOT NULLP PAIRP`

`ADDI a b c` adds the signed 16-bit immediate in `c` to `rB`. `(- x k)` uses it
with `-k`, which gives the same result for fixnums and floats.

Branches test `rA` (and `rB`, or the signed 16-bit immediate in `c` for the `I`
forms) and jump to the target in the following word when the result equals the
`x` flag. With `x = 0` they jump when the test is false, which is what `if`
needs; `not` and `unless` set `x = 1` instead of adding opcodes:

`BR_LT BR_LE BR_GT BR_GE BR_NUMEQ BR_EQ`, the immediate forms
`BR_LTI BR_LEI BR_GTI BR_GEI BR_NUMEQI`, and `BR_NULL BR_PAIR`.

Dynamic state, ported from the stack VM:

| op | effect |
| --- | --- |
| `TRY a bc` | install a handler at `bc`; the condition goes to `rA` |
| `END_TRY` | remove it |
| `RAISE a` | re-raise `rA` |
| `CLEANUP a x` | install cleanup closure `rA`; `x` says whether it takes the outcome |
| `RUN_CLEANUP` | run and remove the newest cleanup |
| `RESTARTS a b` + target word | install `b` restarts from `rA ..`; a transfer jumps to the target |
| `RESULT a` | `rA` = the value a restart transfer left just above the frame top |
| `END_RESTARTS` | remove the newest restart scope |
| `BIND_HANDLERS a b`, `UNBIND_HANDLERS b` | handler-bind |
| `MAKE_PARAM a b c`, `BIND_PARAM a b`, `UNBIND_PARAMS b` | parameters |

## Calls

`CALL` has a fast path for closures whose function has no optional or rest
parameters and is called with exactly its arity. It checks stack and frame
capacity, pushes a frame record, sets unbound only the registers that lie above
the old `sp`, and continues in the callee without leaving `dispatch`. No C call
is made.

Everything else goes through the existing slow path (`rt_apply`/`rt_enter`):
`apply`, rest lists, optionals, generic functions, macros used as functions,
builtins, foreign functions, parameters, records, instances and callable
collections. Builtins keep their interface, `rt_builtin(rt, id, first, count)`,
with arguments read from the caller's registers.

`TAIL_CALL` moves the callee and arguments to the current frame's base and
re-enters the same frame record. A builtin in tail position is called and its
result returned.

`RETURN` writes the result to the frame's base slot, pops the record, restores
`sp` to the floor, and resumes the caller from its saved `ip`, or leaves
`dispatch` when it reaches the stop depth.

## Compiler

The compiler stays a single pass over expanded syntax.

**Registers** are allocated in stack order, as in Lua. Parameters take
registers `1 .. n`. A `let` binding takes the next free register for the
scope's extent. Temporaries are allocated above the bindings and released when
the expression that needed them is done. The main entry point compiles an
expression into a given destination register. A reference to an unboxed local
that is known to be bound uses its register directly as an operand, with no
move. A call is compiled with the callee in the topmost temporary, so every
register above it is dead when the callee's frame is placed there.

**Boxes.** A local needs a heap box only if a closure captures it and it is
assigned after capture: by `set!`, or because a body definition is captured
before it is initialized (mutual recursion). Other captured locals are copied
into the closure by value. Whether a local is captured or assigned can be known
only after the code that refers to it has been emitted, and macro expansion
happens during compilation. So the compiler records these facts per binding,
numbered in creation order, and compiles the top-level form again if it finds
a boxed local that it compiled unboxed. Expansions are cached by form, so a
second pass doesn't expand again, and the facts only grow, so it settles,
normally in at most two passes. Most forms have no boxed locals and compile
once. Boxed parameters are boxed by `BOX` instructions at function entry.

**Self references.** Inside a function bound by a body definition or a named
`let`, the function's own name refers to register 0 unless the binding is
assigned elsewhere. This avoids a box for every self-recursive local function.
A call to self in tail position with exactly the function's arity, and no
optional or rest parameters, becomes moves into the parameter registers and a
backward jump. Named-`let` and `for` loops become real loops.

**Primitive operations.** A call whose head is not shadowed by a local and
names the builtin's own `otium.core` cell, with the arity the opcode handles,
compiles to the primitive opcode. Another global that happens to hold the
builtin (`(def add +)`) is an ordinary call. Each opcode has a fixnum fast path
(and a float path for arithmetic and comparison) and falls back to the builtin
itself for everything else, including overflow, so errors and edge cases
behave as before. Integer overflow keeps the builtin's behavior.

This makes those cells immutable, as `compiler-plan.md` proposes: `def` or
`set!` of one raises "cannot redefine builtin". A namespace can still define
its own `+`, which shadows the core one for code compiled there. Other
builtins stay redefinable for now.

**Tests and branches.** `if`, `when`, `unless`, `cond`, `and`, `or` and `while`
compile their tests in a jump-on-false context. A primitive comparison in that
context becomes a fused compare and branch, and `not` swaps the branch.

**Global definitions** keep going through global cells, so redefinition is
visible to every caller, including self-recursive global functions.

## Code metadata

`Function` keeps its code, `locals` (registers above register 0), arity,
optional count, rest flag, captures and namespace. The per-local `boxed` array
is gone. Each capture records its source (parent register or parent capture)
and whether it is a box. Code reclamation finds constants and child functions
through `CONST` and `CLOSURE` instructions, as now.

## Checks

`make test`, `make sanitize` and `make test-switch` pass, and so do
`compliance.py` (67 pass; `diagnostic output` fails as it did before) and the
Chez differential suites, except `differential-cleanup`, which fails the same
way on the old VM. `prototype/tests/slot-reuse.c` checks register counts instead of the
old slot allocator's. The tests that redefined `+` now use an ordinary global
or expect the error.
