# Native compiler plan

Goal: an optimizing compiler for Otium that produces machine code for the C
runtime. It's written in Otium, with C supplying the parts that touch memory,
the OS and the collector. It should be several times faster than interpreters
and non-optimizing compilers on ordinary code, and within reach of Chez and
SBCL. The same compiler also produces better bytecode for the register VM, for
platforms without a native backend yet.

This supersedes `compiler-plan.md`. Most of that plan's decisions still hold
and are repeated here briefly. What changes:

- Native code is the main target. The VM is a second backend that shares
  everything above instruction selection.
- Chez is going away, so the compiler is developed and run on the C runtime.
  Nothing needs to keep working on Chez.
- The middle of the pipeline is cut down to one main optimization pass
  (Propagate, below) instead of separate type, specialization, CSE and DCE
  passes.

Data structures, the hand-offs between passes, installing code and testing
are in `native-compiler-internals.md`.

## Targets

Measured on the R7RS ports in `benchmarks/r7rs/ports/`, with the current VM,
Chez (safe, optimize-level 2), Guile, CPython and Lua as references.

- **Speed.** Geometric mean within 2x of Chez. No port more than 4x slower than
  Chez. At least 10x faster than CPython and stock Lua on call-, arithmetic- and
  list-heavy ports (fib, tak, nqueens, deriv, destruc, mazefun).
- **Compile time.** Everything is compiled ahead of time, including REPL input,
  so compile time is part of the user experience. A 20-line function at -O1
  should compile in about a millisecond, and all of `lib/` and `manse/`
  (13,000 lines) in a few seconds.
- **Compiler memory.** One top-level form is compiled at a time, its IR lives
  in flat arrays that are reused, and nothing survives compilation except the
  code and its tables. Peak compiler memory is bounded by the largest form,
  not the program.

These are goals to measure against from the first backend on, not promises.

## Decisions

Kept from `compiler-plan.md`:

- **Compile everything ahead of time.** No interpreter tier, no profiling, no
  type feedback. Optimization levels trade compile time for code quality.
- **Builtins are immutable.** Everything bound in `otium.core` is a known
  operation the compiler can fold, inline or specialize. This includes the
  library functions written in Otium (`map`, `for-each`, `get`), which Inline can
  inline.
- **User functions stay redefinable.** Calls go through global cells. Inlining
  a global records a dependency and redefinition recompiles dependents. A
  running frame finishes with the code it started with. `set!` on a global
  only from its own module; `redef!` and `with-redefs` for the rest.
- **One-shot continuations only.** Frames live on stacks.
- **Single return values.**
- **No unsafe level.** Checks disappear only when the compiler proves them
  redundant.
- **Left-to-right evaluation, callee before arguments.**
- **A simple compiler stays forever.** `c/src/compiler.c` and the VM can always
  bootstrap the system.
- **No cross-compiling.** Code is compiled on the machine that runs it.

New or settled here:

- **Fixnum overflow raises an error**, as the C runtime does today. A
  successful fixnum `+` then produces a fixnum, so type and range facts carry
  forward. If bignums arrive later they go on the slow path and inference
  weakens to "integer" for results that could overflow. That's a cost to weigh
  then, not now.
- **Otium code runs on its own stacks, using the hardware `call` and `ret`.**
  C runs on a separate C stack. See Stacks and frames.
- **All allocatable registers are caller-saved.** Values live across a call are
  in stack slots, which is where the collector finds them. This is what Chez
  and SBCL do, and it keeps tail calls, stack walking and the moving collector
  simple.
- **Machine code is position-independent.** Constants, global cells and inline
  caches are reached through a table beside the code. Code bytes never need
  patching when the collector moves objects or an image is loaded.
- **Linear scan register allocation**, not graph colouring. Compile time
  matters more here than the last few percent.
- **The assembler and encoders are Otium.** C installs bytes; it doesn't
  choose them.
- **x86-64 first, then AArch64.** x86-64 because that's the main development
  machine; AArch64 follows closely because of Apple Silicon. Both are designed
  for from the start.

## Pipeline

```
source
  -> reader, expander          (existing)
  1. Resolve                    expanded syntax -> Core
  2. Inline                     inline, fold, simplify (Core -> Core)
  3. Convert                    Core -> block IR, with contification and
                                assignment conversion
  4. Propagate                  types, constants, ranges, CSE, specialization,
                                dead code, all in one pass
  5. Closures                   closure conversion and optimization
  6. Select                     block IR -> operations with representations
  7. Allocate                   registers and stack slots, or VM slots;
                                safepoint maps
  8. Emit                       machine code or bytecode, plus tables
```

Four passes do the optimizing: Inline, Convert (through contification),
Propagate and Closures. The rest translate. Each pass is a named function that
can be switched off where that's meaningful, and the verifier runs after each
in test builds.

Passes 1-5 and the shared half of 6 are target-independent. The VM and each
native target differ in the tables 6 uses, in 7's register model and in 8.

## 1-2. Resolve and Inline

These exist on `wip-vm-peephole` as `lib/otium/compiler/core.ot` and
`cp0.ot` (about 1,100 lines), currently emitting source. They move over largely
as they are, with `cp0.ot` renamed to `inline.ot`:

- Resolve turns expander output into Core: lexical or global references,
  assigned and captured flags per variable, `fix` for mutually recursive
  functions (Fixing Letrec (Reloaded)), and source positions on every node.
- Inline follows Waddell and Dybvig's algorithm, which is Chez's cp0 pass:
  inlining with size and effort budgets, constant folding, copy propagation,
  dead binding removal and test simplification, in one walk. It's also where inlined globals get recorded as
  dependencies.

The branch's loop-invariant and cleanup passes (`cleanup.ot`) aren't carried
forward. Contification and Propagate cover the same ground on the block IR,
with better information.

What Inline doesn't do, on purpose: anything that needs to know types flowing
round a loop, or which branch dominates which. That's Propagate's job, on an IR
built for it.

## The block IR

After Inline, each top-level form becomes a group of functions in a block IR:

- A function has an entry block and a set of blocks. A block has parameters
  and a list of operations, and ends in a terminator: jump to a block with
  arguments, branch, call, tail call, return or raise.
- Every value is defined once. Block parameters stand in for phi nodes.
- A non-tail call is a terminator naming the block that receives its result.
  That makes the IR the same thing as CPS with second-class continuations
  (Kennedy's "Compiling with Continuations, Continued"), which is the form
  `compiler-plan.md` picked. Written as blocks, it maps straight onto register
  allocation and code layout.
- Functions are still values until pass 5. `(closure f x y)` creates one;
  calls to known functions are marked.

Storage is flat: each function's operations, blocks and values are rows in
growable arrays indexed by number, as in Guile's "CPS soup". Operands are
value numbers. Per-value facts (type, use count, representation, register)
are parallel arrays. A pass is a loop over arrays, so the compiler allocates
little and its memory use is predictable, which matters once it runs on small
machines.

## 3. Convert

Convert walks Core once and builds the block IR. It does three jobs on the way
that would otherwise be passes of their own.

**Contification.** A `fix`-bound function that is only ever called, never
passed as a value, and whose calls from outside its group all return to the
same place, becomes blocks in the function that contains it. Calls to it
become jumps with arguments. A short walk over each `fix` group decides this
before converting it. This is how named `let`, `for`, `dotimes`, `while` and
the loops of an inlined `map` or `for-each` become loops, including non-tail
ones like `(+ 1 (let loop ...))`. The VM compiler already does this for named
`let` in tail position, with a recompile when it guesses wrong. Doing it on the
IR removes the guessing.

**Assignment conversion.** A `set!` variable that no closure captures becomes
ordinary SSA values, with block parameters at the joins. One-shot
continuations mean a frame can't be resumed twice, so this is never visible.
Captured-and-assigned variables get boxes.

**Refinement points.** Where a branch tests a value, such as `(pair? x)`,
`(nil? x)`, `(< i n)` or a `match` pattern, each successor gets a fresh name
for the value with the fact attached: `x'` is a pair on the true side. This is
the trick HotSpot uses with cast nodes and SSI uses with sigma nodes. It means
Propagate needs no special handling for facts that hold on one edge only;
they're ordinary values with ordinary types.

## 4. Propagate

This is the main optimization pass. It combines sparse conditional constant
propagation (Wegman and Zadeck) with type and range inference, then rewrites
the function in a walk over its dominator tree. Doing these together finds
more than running them in sequence any number of times (Click and Cooper,
"Combining Analyses, Combining Optimizations"). A branch that constants prove
dead stops feeding types into the join, which lets a type check fold, which
removes the slow path that was keeping a value generic.

### The lattice

Each value gets:

- A set of types, as a bitmask: fixnum, float, pair, empty list, nil, false,
  true, char, string, symbol, keyword, array, table, closure, record, instance,
  foreign pointer, other. "Unknown" is all bits.
- For fixnums, a range `[lo, hi]`.
- Optionally a constant, a record type, a class, or a known function.

Primops have transfer functions in a table written in Otium beside the existing
`prims.ot`: `car` of anything returns unknown; `+` of fixnum `[a,b]` and fixnum
`[c,d]` returns fixnum `[a+c, b+d]` when that range fits, and otherwise fixnum
(the overflow raises); `pair?` of a value known to be a pair is `#t`.

### The analysis

Optimistic, as in SCCP: every block starts unreachable and every value starts
at "no information". A worklist marks edges executable and pushes values up the
lattice until nothing changes. Loops get the right answer: a counter that
starts at 0, is only incremented, and is compared against a fixnum `n` before
each increment is a fixnum in `[0, fixnum-max - 1]`, so `(+ i 1)` can't
overflow and needs no check. Ranges widen to unbounded after a few rounds at a
loop header, so the analysis always terminates.

### The rewrite

One walk over the dominator tree, using the facts:

- Replace values known to be constant, and fold branches whose outcome is
  known. Drop blocks that never became reachable.
- Specialize primops. Generic `+` becomes checked fixnum add with a generic
  slow path, unchecked fixnum add when the range allows, or float add. `car` of
  a known pair becomes a plain load. `(get t k)` on a known table becomes a
  table lookup without the dispatch.
- Remove checks the facts prove: type checks, unbound checks, arity checks on
  calls to known functions.
- Common subexpressions, by hashing operations scoped to the dominator tree.
  Only operations whose effect set allows it are candidates (see Effects).
- Turn calls whose callee is a known function into direct calls.

Then a sweep removes operations whose results are unused and that have no
effects.

### Effects

Every primop has an effect set: allocates, reads mutable memory, writes mutable
memory, may raise, calls unknown code, may switch threads. Unknown calls have
all of them. CSE and dead code removal consult this and nothing else. An
operation that may raise is never removed or moved past another effect, so
errors happen where the source says they happen.

### What Propagate doesn't do

No loop-invariant code motion, no bounds-check hoisting, no escape analysis,
no interprocedural type inference. Each is worth having eventually. None
belongs in the first version, and each should earn its place on the benchmarks
before it's added. Inlining only happens in Inline; Propagate never inlines, so it can't
grow code.

## 5. Closures

Flat closures, with the optimizations from Keep, Hearn and Dybvig's
"Optimizing Closures in O(0) Time":

- A function whose every use is a direct call is "well-known". It needs no
  closure value if it has no free variables, and otherwise its free variables
  can be passed as extra arguments.
- Functions with no free variables are static constants and cost nothing to
  create.
- Mutually recursive closures share one object where that's safe.
- Free variables that are themselves known closures or constants are dropped.

This runs after Propagate because Propagate deletes code, and deleted code
often held the only reference that made a variable free.

## 6. Select

Select turns each block IR operation into one or more operations from a
per-target table, and gives every value a representation. It works like SBCL's
virtual operations (VOPs).

An operation is defined once, in Otium, with:

- Its operands and result, each with an allowed representation: tagged value,
  raw fixnum (untagged 64-bit integer), raw double, or raw word.
- Constraints: which operands may be immediates, whether the result must share
  a register with an operand (x86 two-address forms), scratch registers it
  needs, and whether it's a safepoint.
- Its slow path, if any, as a label to out-of-line code.
- A cost, used when several operations match.
- Per backend: the VM opcode it becomes, and a native template per target.

So `fx+/checked` is one operation with a VM opcode (`ADD` today, or a new
`ADD_FX` that skips the float path) and an x86-64 template that is four
instructions and a branch to the slow path.

**Representations.** A float that is produced by float arithmetic and consumed
by float arithmetic stays a raw double in a register or stack slot. It's boxed
only where it flows into something that wants a tagged value: a call, a store
into a pair or array, a return. Same for raw integers, which matters for
`bitwise-*` work and FFI. Where representations disagree, Select inserts box
and unbox operations, and puts them on the edge where the value escapes, not
at its definition.

**Allocation.** Allocations in the same block are combined into one bump of the
allocation pointer and one limit check (OCaml's Comballoc). Building a
three-element list costs one check, not three.

**Branches.** A comparison used only by a branch fuses into a
compare-and-branch. A truth test on a value that isn't known to be a boolean
uses Otium's encoding: `nil` is 2 and `#f` is 6, so `(v | 4) == 6` tests for
both in two instructions.

## 7. Allocate

**Native.** Linear scan on SSA, after Wimmer and Franz ("Linear Scan Register
Allocation on SSA Form", 2010). It's fast, works directly on the block IR
without leaving SSA first, and handles operation constraints through fixed
intervals. Block arguments become parallel moves at the end of each
predecessor, sequenced with a scratch register for cycles. Values live across
a call are spilled, because no allocatable register survives a call.

**VM.** The same liveness, with unlimited registers: values whose lifetimes
don't overlap share a slot. Arguments go straight into the slots the call
needs, which removes most of the argument `MOVE`s that are the VM's top opcode
today.

**Safepoint maps.** At every safepoint (calls, allocation slow paths, polls),
liveness says which slots and registers hold live tagged values. That map is
the collector's whole view of the frame. Dead values are never retained, raw
doubles and integers are never traced, and the VM's slot-clearing instructions
go away. Identical maps are shared.

## 8. Emit

Native: lay out blocks with slow paths at the end of the function, expand each
operation's template through the target's encoder, fix branch offsets
(choosing short forms where they reach), and build the tables. Hand the bytes
and tables to C to install.

The encoders are Otium functions writing into a bytevector, one file per
target. x86-64 needs the integer, SSE2 and branch subset, a few hundred lines.
AArch64 is simpler to encode and similar in size.

VM: write instructions as `compiler.c` does now, with the same tables.

Tables, per code object:

- Return points: offset of each call's return address, the frame size there,
  and its safepoint map.
- Source positions per return point and per instruction that can raise, with
  an inline tree so backtraces show inlined frames, as SBCL does.
- The link table layout (constants, cells, caches).
- Dependencies: the (cell, version) pairs the code assumed.

## Runtime model for native code

This is what the C runtime has to provide. Some of it changes the runtime for
the VM too; those changes are worth making first, because they're measurable
on the VM and don't wait on a backend.

### Values

Fixnums keep the low bit set and heap pointers stay 8-byte aligned. Two
changes, both measured on the VM before committing to them:

- **Tag pairs.** Low bits `100` are unused today: immediates use `010` and
  `110`. Giving pairs that tag makes `pair?` one test on the register, with no
  load, and the tag folds into the load's displacement, so `car` and `cdr`
  cost nothing extra. List code checks `pair?` constantly.
- **Smaller headers.** Every object has a 16-byte header, so a pair is 32 bytes
  where Chez's is 16. For allocation-heavy ports (nboyer, deriv, gcbench) that
  doubles the bytes allocated and copied. An 8-byte header gets a pair to 24.
  Getting to 16 needs headerless pairs, which needs the collector to tell a
  pair from its address, for example by keeping pairs in their own pages.
  Start with the 8-byte header.
- **Lazy identity.** Each allocation increments a global identity counter that
  only eq-hashing uses. Native allocation should be a bump and a header store.
  Assign identities the first time an object is hashed.

Fixnum tag 1 costs one extra instruction on add and subtract compared with tag
0. Not worth changing the embedding API for; the compiler keeps loop counters
raw where it can.

### Global cells

Globals are in `rt->globals`, which `oti_grow` reallocates, so their addresses
move. Native code needs cells at fixed addresses: allocate them in chunks that
never move.

Each cell holds its value and an **entry address**, as SBCL's fdefns do. A call
to a global loads the value into the closure register and calls through the
entry, with no checks at the call site. Storing into the cell sets the entry:

- a native closure: its general entry
- a VM closure: the VM transition stub
- a table, array, keyword, instance or record: the callable-collection stub
- unbound: a stub that raises the unbound error

The cell also carries the version and watched bit that dependency tracking
uses.

### Closures and code objects

Code lives in a separate code space: executable memory allocated by C, never
moved by the collector. A code object is the machine code, its link table and
its metadata tables. Code is freed by the existing reclamation pass when no
closure, frame or cell refers to it.

A native closure is a heap object holding a raw code address (opaque to the
collector) and its free variables. The link table holds heap constants, cell
addresses and inline cache slots. It's a collector root, and the collector
updates the constants in place. Code reaches every entry with a pc-relative
load (`mov rax, [rip + n]` on x86-64, `ldr` from a literal on AArch64), which
is why code never needs patching.

### Calling convention

- Dedicated registers: the runtime state, the allocation pointer, and the
  allocation limit if registers allow (x86-64: `r15`, `r14`; AArch64: `x28`,
  `x27`, `x26`).
- Closure in a fixed register, argument count in another.
- The first four arguments in registers on x86-64 and the first eight on
  AArch64. The rest go to an argument area in the runtime state, which the
  callee copies into its frame on entry. That makes tail calls with any number
  of arguments as cheap as with a few, at the cost of memory traffic for long
  argument lists, which are rare.
- Result in a fixed register.
- Each function has a general entry, which checks the argument count and
  handles optionals and rest lists, and a direct entry after those checks.
  Known calls with the right count jump to the direct entry.
- A tail call drops the frame and jumps.

A global call is three instructions: load the closure from the cell, set the
count, `call [cell + entry]`. A known local call is a direct `call`. An unknown
call checks the closure's kind before calling through its code address, and
everything that isn't a closure goes to the slow path.

### Stacks and frames

Otium frames are on an Otium stack, one per green thread, and use the hardware
`call` and `ret`, so returns are predicted. A frame is a return address, spill
slots and outgoing space, of a fixed size per return point. There's no frame
pointer chain: the walker reads a return address, looks up its return point in
the code object's table, and gets the frame size and the map. A global sorted
table (or page map) finds the code object for an address.

C runs on the C stack. Calls from native code into runtime helpers or foreign
functions go through a stub that saves the Otium stack pointer in the runtime
state and switches to the C stack. Calls from C into Otium (`ot_call`,
callbacks) switch back and record a boundary, so the walker knows where each
run of Otium frames starts and ends.

That separation has two benefits. Otium stacks need no headroom for C, so
green threads can start with small stacks. And because nothing points into an
Otium stack except saved stack pointers the runtime owns, a stack can be grown
by copying it to a larger block when it overflows, as Go does. That suits low
memory targets better than large fixed stacks.

`ot_call` across a C callback still nests, as it does in the VM today.

### Polls

Each function entry compares the stack pointer against a limit in the runtime
state: `cmp rsp, [r15 + limit]; jb slow`. Loops without calls do the same at
their back edge. Setting the limit high forces every thread into the slow path
at its next check, which handles stack overflow, interrupts, collection
requests and preemption with one compare and a predicted branch. Gambit and
Go both work this way.

### The collector

The heap stays a moving semispace collector for now. What changes is root
finding:

- The walker visits each run of Otium frames using the return point maps.
- At an allocation slow path or a poll, live values may be in registers. Those
  stubs save every allocatable register to a known area, the map lists which
  ones hold tagged values, and the collector updates them there before the stub
  restores them.
- Derived pointers never live across a safepoint. Select recomputes addresses
  after one instead.

A generational collector, when it comes, needs a write barrier. Select is where
it goes: one operation template per store kind.

### Errors, `try` and cleanups

The runtime's try records gain an Otium stack pointer and a resume address.
Raising from native code calls the runtime, which finds the handler as it does
now, runs cleanups in between, and resumes by restoring the stack pointer and
jumping. Restarts work the same way. The `prompt`/`abort` and
`dyn-push`/`dyn-pop` forms in Core lower to runtime calls at first, and to
inline code later where it's simple (pushing a parameter binding is a few
stores).

### Green threads and continuations

One-shot continuations and thread switches swap Otium stacks: save the stack
pointer, load another. A stub of a few instructions, emitted by the backend.

### VM and native together

On a platform with a backend, VM code only comes from the simple compiler,
during bootstrap and when the optimizing compiler can't be loaded. VM and
native frames don't share a layout. A call from one to the other goes through a
transition stub and nests a dispatch loop or a native entry. A tail call across
the boundary returns to the nearest stub with a request to make the call,
trampoline style, so mutual tail recursion between VM and native code still
runs in bounded space.

## The VM backend

The VM shares passes 1-5, Select's representation choices and Allocate's
liveness. What it gets that `compiler.c` can't give it:

- Inlining across functions and of library code (`map`, `for`, small
  helpers), with dependency tracking.
- Loops from contification, including non-tail ones.
- Typed opcodes where Propagate proved types: fixnum operations without the
  tag test or float path, `car` and `cdr` without the check, direct calls to
  known functions without the closure and arity checks.
- Slot allocation by liveness, with arguments placed where calls need them.
- Safepoint maps, which allow raw float slots and drop the slot clearing.

New VM opcodes are only worth it if they remove instructions or checks the
benchmarks show. Typed arithmetic, unchecked pair access and direct calls are
the expected ones. Expect a 1.5-3x gain over the simple compiler on the ports;
the VM's dispatch is already instruction-count bound, so fewer instructions is
the lever.

## Generic functions, classes and keyword calls

As in `compiler-plan.md`: slot access with a constant key and generic calls use
per-site inline caches keyed on layout and generic epoch, stored in the link
table. Native code inlines the cache check (a layout or class compare and a
branch) and calls a stub on a miss. When Propagate knows the exact class, the
compiler uses the slot offset directly and records a dependency on the layout.
`(x :key)` with unknown `x` is one operation whose cases are closure call,
instance slot cache and table lookup.

## Who writes what

Otium, under `lib/otium/compiler/`:

- all eight passes
- the primop table: transfer functions, effects, folding
- operation definitions and templates for each target
- encoders for x86-64 and AArch64
- the runtime stubs: entry and exit transitions, poll, allocation slow path,
  stack switch, callable-collection call, unbound call. They're machine code,
  so the backend emits them at startup like any other code, and C keeps no
  assembly. That keeps the sqlite-style amalgamation a set of plain C files.
- the reference interpreters and the verifier

C, as `ot_`/`oti_` functions the compiler calls through builtins:

- executable memory: allocate, write, make executable, free; W^X on Linux,
  `MAP_JIT` and `pthread_jit_write_protect_np` on macOS, instruction cache
  flushing on AArch64
- installing a code object: register its address range, return point table,
  link table roots and metadata
- stable global cells with entry addresses, and setting the entry on every
  store
- the stack walker for collection, unwinding and backtraces
- runtime helpers native code calls: generic arithmetic, allocation slow path,
  errors with their conditions, `apply`, rest list building, table operations,
  generic dispatch on a cache miss
- Otium stack allocation, growth and guard pages
- a `/tmp/perf-<pid>.map` writer so `perf` can name compiled functions, and
  later the GDB JIT interface

Foreign calls from native code keep using libffi to start with. Emitting
direct calls for known signatures is a later improvement that the backend can
make on its own.

## Compile speed

The compiler itself is compiled by the compiler, so its own code quality
matters as much as any benchmark's. Rules that keep it fast:

- Flat arrays, numbered values, no per-node tables. The source compiler on the
  branch uses a table per variable and per node; that's fine for Core, but the
  block IR shouldn't.
- One form at a time, IR arrays reused between forms.
- Linear scan, one pass of Propagate, budgets on Inline.
- -O0 skips Inline and Propagate's analysis (it still runs the cheap rewrites:
  folding and known calls), and Allocate uses a simpler allocator. Still
  native code, still fast enough to debug with. Which level the REPL defaults
  to waits on measurements.

Compile time per form is recorded from the first working backend on, and
tracked like any other benchmark.

## Correctness

The checks in `compiler-plan.md` carry over: a Core interpreter and a block IR
interpreter in Otium as references, a verifier after every pass, pass switches
and a fuel counter for bisecting to the rewrite that broke something, a random
program generator, and agreement across optimization levels.

What changes now that the oracle isn't Chez:

- **The simple compiler is the oracle.** Every test runs under `compiler.c` on
  the VM, under the optimizer on the VM, and under the optimizer natively.
  Same runtime and same representations, so any difference is a compiler bug.
- **Native against VM.** Both backends share passes 1-5, so a difference
  between them is in Select, Allocate or Emit.
- **Safepoint maps.** A debug mode writes poison into every slot and register
  a map doesn't list as live, at each safepoint, and collects at every
  allocation. A missing entry in a map then fails at once.
- **Encoders.** Each target's encoder is checked against an external
  assembler's output for every instruction form it emits (a test-only use of
  `as` or `llvm-mc`).

## Phases

Each ends with something that runs and is tested.

0. **Runtime groundwork, measured on the VM.** Stable global cells, pair tag,
   8-byte headers, lazy identity. Each change shows its effect on the ports
   before the next.
1. **Front end on the C runtime.** Move Resolve and Inline from
   `wip-vm-peephole`. The C runtime has `macroexpand-all` now, so the old
   blocker is gone. Exit: the compiler's source-to-source output passes the
   test suites when run on the C runtime.
2. **Block IR.** Convert with contification and assignment conversion, the IR
   interpreter, the verifier. Exit: tests pass through the IR interpreter.
3. **Propagate.** Exit: tests pass through the IR interpreter after
   Propagate, and the generator finds no disagreements.
4. **VM backend.** Closures, Select, slot allocation, bytecode, safepoint maps
   in the VM. Exit: tests and ports run; the simple compiler and the optimizer
   agree on everything; the compiler compiles itself to bytecode.
5. **x86-64.** Encoder, templates, linear scan, stubs, code space, walker, the
   transition stubs. Exit: tests and ports pass natively, the compiler compiles
   itself natively, and the first numbers against Chez.
6. **AArch64,** on Linux and macOS.
7. **Images with native code.** Because code is position-independent, saving
   an image writes code bytes as they are and rebuilds link tables on load.

Phase 4 comes before native code on purpose. It tests every shared pass
against the existing VM with nothing new below it, and it's what platforms
without a backend will run.

## Open questions

- Which optimization level the REPL uses, once compile times are measured.
- Loops and redefinition: whether to avoid inlining globals into a function
  containing a loop back edge (the `compiler-plan.md` preference) so a
  long-running loop like Manse's event loop picks up redefinitions.
- Whether to add checked type declarations. With no type feedback, they're the
  main way to tell the compiler something it can't prove.
- How far to take headerless pairs, after the 8-byte header is measured.
- What a green thread may observe at a poll when preemption is on. Propagate
  assumes globals and mutable data can change at any poll, which is correct
  but may cost something in tight loops.

## References

- Waddell and Dybvig, "Fast and Effective Procedure Inlining" (the basis of Chez's cp0, which Inline follows).
- Ghuloum and Dybvig, "Fixing Letrec (Reloaded)".
- Kennedy, "Compiling with Continuations, Continued".
- Fluet and Weeks, "Contification Using Dominators".
- Wegman and Zadeck, "Constant Propagation with Conditional Branches".
- Click and Cooper, "Combining Analyses, Combining Optimizations".
- Keep, Hearn and Dybvig, "Optimizing Closures in O(0) Time".
- Wimmer and Franz, "Linear Scan Register Allocation on SSA Form".
- SBCL internals on VOPs and representation selection, and CMUCL's design
  notes on IR1 and IR2.
- Leroy and others, OCaml's native backend (allocation combining, the
  allocation pointer register).
- Feeley, Gambit's GVM and its stack-limit interrupt checks.
- Loko Scheme, https://scheme.fail/manual/loko.html
- Andy Wingo on Guile's CPS soup, wingolog.org.
