# Native compiler internals

The data structures, the hand-offs between passes, how a compiled function
becomes executable memory, and how the whole thing is tested. Companion to
`native-compiler-plan.md`, which says what the compiler does and why. Nothing
here is built yet.

## Overall shape

The compiler works on one **unit** at a time: one top-level form, with every
function nested inside it. A unit goes through the passes and comes out as one
installed code object with an entry per function. Nothing from the IR outlives
the unit except what goes into the code object's tables.

```
expanded form
  | Resolve            Core tree + variable records
  | Inline             Core tree (rewritten) + dependency set
  | Convert            block IR
  | Propagate          block IR (rewritten) + type columns
  | Closures           block IR, closure-converted
  | Select             LIR: operations on virtual registers
  | Allocate           LIR + locations, block order, frame, maps
  | Emit               code blob: bytes, fixups, tables
  | install (C)        code object, entries, closures
```

For the VM, only Resolve and Inline run, and the Core goes back to an Otium
form for `compiler.c`.

Two IR families, chosen to suit their passes:

- **Core** is a tree. Inline is a recursive walk with an environment, and trees
  suit that. It's what the source compiler on `wip-vm-peephole` already uses.
- **The block IR and LIR** are flat tables of numbered rows. Every pass from
  Convert on is a loop over arrays, analysis results are more arrays indexed by
  the same numbers, and resetting a unit just sets lengths back to zero.

## Shared infrastructure

### The unit record

One `defrecord` carried through every pass:

| field | contents |
| --- | --- |
| `ns` | namespace the form is compiled in |
| `level` | 0, 1 or 2 |
| `target` | `:vm`, `:x86-64` or `:aarch64` |
| `source` | file id; positions elsewhere are byte offsets into it |
| `deps` | assumptions made: list of `(cell-id version)` and `(layout-id version)` |
| `fuel` | remaining rewrite budget, for bisection |
| `core` | the Core tree (passes 1-2) |
| `ir` | the block IR tables (passes 3-5) |
| `lir` | the LIR tables (passes 6-8) |
| `diagnostics` | warnings found along the way (unreachable code, known type errors) |

Each pass is `(pass unit) -> unit`, and is registered by name with a flag that
says whether it can be switched off. The driver runs the verifier for an IR
after any pass that produces it when `--verify` or the test build is on.

### Columns

Flat tables are built from one helper: a **column**, a record holding an Otium
array and a length. `col-push!` appends, `col-ref` and `col-set!` index, and
`col-reset!` sets the length to zero but keeps the array, so the next unit
reuses the storage. Every table is a group of columns indexed by the same row
number: structure of arrays, as in a database. Values in columns are fixnums,
symbols or small records, never tables. That keeps the GC traffic per unit
near zero once the columns have grown to fit.

Ranges of variable length (operands, block parameters, successors, use lists)
go in one shared column per table, and rows hold `(start, count)` pairs into
it. Most of these are append-only, written while a row is built and never
grown afterwards. Where something must grow later (use lists), it's rebuilt in
one counting pass (count, prefix sum, fill), the CSR layout.

### Bitsets

Liveness, reachability and type sets are bitsets: a column of fixnums, 60 bits
per word so every word is a positive fixnum and `bitwise-and` and friends never
leave fixnum range. Operations: set, clear, test, union into, equal, iterate.

### Numbering and identity

- Values, operations, blocks and functions are fixnum ids within the unit.
- Globals are referred to by **cell id**, the runtime's index for a global
  cell. Resolve turns names into cell ids once.
- Primops and LIR operations are referred to by small integer ids into
  static tables (below).
- Source positions are byte offsets in the unit's source file. Every row that
  can raise or call keeps one. Inlining adds an inline-tree index next to it:
  row `i` of the inline tree says "inlined from function F at position P, inside
  row `j`".

### The primop table

One row per builtin the compiler knows, defined in Otium
(`compiler/prims.ot`, growing from the branch's version):

| field | contents |
| --- | --- |
| `id`, `name`, `cell` | identity, and the `otium.core` cell it stands for |
| `arity` | required count, and whether variadic |
| `effects` | bitmask: allocates, reads, writes, raises, calls-unknown, switches |
| `fold` | function from constant arguments to a constant, or nil |
| `transfer` | function from argument facts to result fact (Propagate) |
| `refines` | for predicates: the type set proven on the true and false edges |
| `specialize` | ordered list of `(guard-on-arg-types . primop-id)` |
| `checked` | whether the operation itself checks its argument types |

Specialized variants (`fx+/checked`, `fx+`, `fl+`, `car/unchecked`) are rows in
the same table, with their own effects. `fx+` with ranges that can't overflow
has no `raises` bit, which is what lets CSE and dead code removal touch it.

## 1. Resolve

**In:** the expander's output for one form, with source positions recorded by
`expander-locate`.

**Out:** a Core tree.

Core is as on the branch, with two changes for speed: nodes become records
(one `defrecord` per node kind) instead of tables with an `:op` key, and
globals hold a cell id instead of a symbol.

| node | fields |
| --- | --- |
| `Const` | value |
| `LRef`, `LSet` | var, value |
| `GRef`, `GSet`, `GDef` | cell, value, kind (`def`, `def-`, `defonce`, `defparam`) |
| `If` | test, then, else |
| `Seq` | exprs (list) |
| `Let` | var, value, body |
| `Fix` | vars, fns, body |
| `Fn` | params, optionals, rest, body, name, position |
| `Call` | fn, args |
| `PrimCall` | primop id, args |
| `Dyn` | kind (`try`, `handlers`, `params`, `restarts`, `cleanup`), parts |

Every node also has a position. Variables are records:

| field | contents |
| --- | --- |
| `id`, `name` | identity, and the source name for printing |
| `refs`, `sets` | counts, kept up to date by Inline |
| `captured` | referenced from a nested `Fn` |
| `binding` | the node that binds it, for Inline's lookups |

Resolve does name resolution, sets the flags, and applies Fixing Letrec
(Reloaded) so that most `letrec` becomes `Fix`, with explicit checks for the
rest.

## 2. Inline

**In and out:** Core. Inline rewrites the tree, copying when it inlines.

Data structures, following Waddell's thesis:

- **Environment:** a persistent map from var to its current replacement, an
  assoc list or small array per scope (scopes are shallow).
- **Operand record**, one per argument of a call being inlined: the argument's
  expression, its environment, the result of visiting it (computed lazily,
  once), its size, and whether it's been referenced. Laziness is what lets Inline
  drop arguments that end up unused without visiting them.
- **Counters:** a size counter and an effort counter per inlining attempt.
  Running out of either abandons the attempt and keeps the call. Each is a
  record with a limit and a value, plus the continuation to escape to; on the
  C runtime that escape is a one-shot continuation or an early return through
  a flag.
- **Context:** an enum passed down: value, test, effect, or application with
  operands. "Test" lets `(if (if a b #f) ...)` collapse; "effect" drops pure
  work.

Inline only inlines a global whose definition declares `inline` (see
Declarations in the plan), and adds `(cell-id version)` to `unit.deps` for each
one. It never inlines a cell that has been `set!`, a `defonce`, or data.

Library rewrites run here too, as rows beside the primop table: a pattern on
the builtin's name and argument facts, and the Core it becomes.

**Hand-off to Convert:** a Core tree whose `refs` and `sets` counts are exact,
since Convert uses them to decide assignment conversion and contification.

## 3. Convert

**In:** Core. **Out:** the block IR.

### Block IR tables

**Functions**, one row each:

| column | contents |
| --- | --- |
| `fn-name`, `fn-pos` | for backtraces and the perf map |
| `fn-parent` | enclosing function, or none for the unit's top-level thunk |
| `fn-params` | range of value ids |
| `fn-optional`, `fn-rest` | count of optionals, rest flag |
| `fn-entry` | entry block id |
| `fn-blocks` | range of block ids |
| `fn-free` | free variable value ids (filled by Closures) |
| `fn-kind` | escaping, well-known, static (filled by Closures) |

**Blocks:**

| column | contents |
| --- | --- |
| `blk-fn` | owning function |
| `blk-params` | range of value ids |
| `blk-first`, `blk-count` | range of operation ids |
| `blk-term` | terminator kind |
| `blk-targs` | range of operand value ids for the terminator |
| `blk-succ` | range of successor block ids, with per-edge argument ranges |
| `blk-pos` | position of the terminator |
| `blk-loop` | loop depth (filled when dominators are computed) |

**Operations** (the non-terminator instructions):

| column | contents |
| --- | --- |
| `op-code` | primop id or IR opcode |
| `op-args` | range of value ids |
| `op-result` | value id, or none |
| `op-aux` | a constant, cell id, function id or slot index, depending on the code |
| `op-pos` | position and inline-tree index |

**Values:**

| column | contents |
| --- | --- |
| `val-def` | defining operation, or block and parameter index |
| `val-name` | source variable, for dumps and debug info |
| `val-uses` | use count; use lists are built on demand in CSR form |

IR opcodes besides primops: `const`, `global-ref`, `global-set`, `global-def`,
`closure` (function id and captured values), `box`, `box-ref`, `box-set`,
`refine` (value and type set), and the dynamic-state operations `push-try`,
`pop-try`, `push-handlers`, `pop-handlers`, `push-param`, `pop-param`,
`push-cleanup`, `pop-cleanup`.

Terminators:

| kind | operands and successors |
| --- | --- |
| `jump` | target block, arguments |
| `branch` | value, then block, else block |
| `branch-prim` | primop predicate and its arguments, then, else (no boolean materialized) |
| `call` | callee kind (unknown value, global cell, known function), arguments, return block |
| `tail-call` | same, no return block |
| `return` | value |
| `raise` | condition |

A non-tail call ends its block. Its result is the return block's first
parameter. That's the CPS shape, and it means every point where unknown code
can run is a block boundary, which simplifies Propagate (facts about mutable
memory die at block entry from a call) and Allocate (all registers die there).

### What Convert does

It's one recursive walk with an environment from Core variables to value ids,
plus a "current block" being filled. Each Core expression is converted with a
continuation that's either "put the value here and keep going", "jump to this
block with the value", "branch on it to these blocks", or "return it". That
continuation argument is how tail position, test position and value position
are told apart without separate passes.

- **Assignment conversion.** A variable with `sets > 0` and not `captured`
  is tracked in the environment as "current value id". A `set!` updates the
  environment. At a join, each such variable that differs between predecessors
  becomes a block parameter. Captured and assigned variables become `box`.
- **Contification.** Before converting a `Fix`, a short check over the group:
  every reference to each function is in call position, and every call from
  outside the group has the same continuation. If so, each function becomes a
  block whose parameters are its parameters, and calls become `jump`. If not,
  the functions are converted as functions.
- **Refinement.** When the test of an `If` is a predicate on a variable,
  `(pair? x)`, `(nil? x)`, `(< x n)` and so on, the then-branch is converted
  with `x` bound to a new value `(refine x pair)` and the else-branch with the
  complementary fact. Because the walk follows the tree, the region where the
  refined name is visible is exactly the dominated region. No dominance
  computation is needed.
- **`try` and other dynamic forms.** `push-try` names a handler block that
  receives the condition as its parameter. A variable assigned inside a try
  body and read after the handler gets a box, because its value at the time of
  the raise isn't known statically. That's rare, and keeps the IR free of
  exceptional edges. The same rule applies to restarts.

**Hand-off to Propagate:** each function's blocks in the order Convert created
them, accurate use counts, refinement points in place, no boxes except for
captured-and-assigned variables and the `try` case.

## 4. Propagate

**In and out:** block IR, per function, inner functions first. A known inner
function's result type then feeds calls to it.

### Data

- **Lattice columns, per value:** `ty-bits` (type set as a fixnum bitmask),
  `ty-lo` and `ty-hi` (fixnum range, meaningful when the fixnum bit is set),
  `ty-const` (constant or an unset marker), `ty-extra` (record type, class or
  function id). "No information yet" is `ty-bits = 0`.
- **Reachability:** a bitset over blocks and one over edges. An edge id is
  the index into the block's successor range.
- **Worklists:** one of edges, one of values, each a column plus an "on list"
  bitset so nothing is queued twice.
- **Use lists:** built at the start, in CSR form, so a changed value can find
  the operations that read it.
- **Dominator tree:** `idom` and reverse postorder, computed with Cooper,
  Harvey and Kennedy's iterative algorithm over the reachable blocks once the
  analysis has settled. Children in CSR form. Loop depth falls out of the
  back edges found in the same walk.
- **CSE table:** open addressing over columns, keyed by a hash of `op-code`,
  `op-aux` and the argument value ids. Scoped by the dominator tree with an
  undo log: each entry added records its slot, and leaving a dominator subtree
  clears the slots it added.
- **Widening counters:** per loop-header parameter, how many times its range
  has grown. After three, the range goes to the full fixnum range.

### Output

A new block IR for the function, written into the second of two sets of
tables, which then swap. Writing fresh tables is simpler than editing in place:
deleted blocks and operations just aren't copied, operations are renumbered
densely, and the verifier sees clean tables. The type columns are carried
across under the new numbering, because Select uses them to choose
representations.

Rewrites made: constants substituted, branches folded, unreachable blocks
dropped, primops replaced by specialized rows, proven checks dropped, `call`
with a known function marked as known, CSE hits replaced with the earlier
value, unused pure operations dropped.

## 5. Closures

**In and out:** block IR, whole unit.

### Data

- **Free variables per function:** a sorted column of value ids. Computed in
  one walk: a use whose definition belongs to another function is free in the
  using function and in every function between them.
- **Reference kinds per function:** counts of uses as a known callee and as
  a value. A function used only as a known callee is well-known.
- **Groups:** functions created by the same `closure` operations in one block
  that refer to each other, as candidates for sharing one closure object.

### Rewrite

- Every function that still needs an environment gets an extra first
  parameter, `env`.
- Free variable uses become `(env-ref env i)`.
- `closure` operations become `(make-closure fn-id n)` plus initializing
  stores, or disappear for static functions (no free variables), whose closure
  is a constant created at install time.
- Well-known functions with free variables take them as extra parameters when
  there are few of them (four or fewer to start), and their callers pass them.

`fn-kind` is set to `static`, `well-known`, or `escaping`, and `fn-free` to the
final environment layout.

**Hand-off to Select:** every function closed except through `env`. Every
call marked as unknown, global, or known with a function id. Type columns
intact.

## 6. Select

**In:** closure-converted block IR with types. **Out:** LIR.

### The operation table

One row per LIR operation, defined in Otium with a `defop` macro, shared by
all backends:

| field | contents |
| --- | --- |
| `id`, `name` | identity |
| `args` | per argument: representation, and whether an immediate is allowed and what range |
| `results` | per result: representation, and whether it must share a register with an argument |
| `temps` | scratch registers needed, by class (general, float) |
| `fixed` | arguments or temps that must be in a particular register, per target |
| `flags` | `call` (clobbers all registers), `safepoint`, `preserving` (a safepoint whose stub saves registers), `branch`, `raises`, `has-slow` |
| `cost` | instructions, roughly; used when several rows match |
| `vm` | the VM opcode and how operands map to its fields |
| `x86-64`, `aarch64` | template functions: `(fn (asm operands temps labels) ...)` |

Representations: `tagged`, `fx` (raw 64-bit integer, a fixnum's value), `fl`
(raw double in a float register), `word` (raw machine word, for FFI and
addresses).

Example. `fx+/checked`: two `tagged` arguments (second may be an immediate
fixnum), one `tagged` result, one temp, flags `has-slow`. Its x86-64 template:

```
mov  tmp, a
sub  tmp, 1          ; drop one tag bit
add  tmp, b          ; or add tmp, imm*2
jo   slow
mov  result, tmp
```

with the slow path calling the generic `+` through a preserving stub and
jumping back.

### LIR tables

Same shape as the block IR, renamed:

- **LIR blocks:** parameters, instruction range, successors with argument
  ranges, a `cold` flag for slow-path blocks, loop depth.
- **LIR instructions:** `li-op` (operation id), `li-args` (range of operands),
  `li-results` (range of vreg ids), `li-temps` (range, assigned later),
  `li-pos`, `li-safepoint` (safepoint id or none).
- **Operands:** either a vreg id or an immediate, encoded in one fixnum column
  with a tag bit.
- **Vregs:** `vr-rep`, `vr-hint` (a vreg or physical register to prefer),
  `vr-spill-weight` (uses weighted by loop depth).

### What Select does

1. **Choose representations.** For each block IR value: `fl` if it's produced
   by a float operation and the weighted count of uses that want `fl` exceeds
   those that want `tagged`. Same for `fx`. Otherwise `tagged`. Block
   parameters choose from their incoming arguments and uses together, iterated
   to a fixed point over each loop (it settles quickly; the lattice has three
   points).
2. **Pick rows.** For each operation, the cheapest row whose argument
   representations and type guards match. Mismatched representations get a
   coercion row on that use: `box-fl`, `unbox-fl`, `tag-fx`, `untag-fx`. A
   coercion is placed on the edge or use where the value needs it, so a float
   loop variable boxes once at the loop exit, not every iteration.
3. **Lower calls.** Arguments to the convention's registers become fixed
   operands. A global call becomes `load-cell`, `load-closure`, `call-entry`.
   A known call becomes `call-direct fn-id entry`. A call row has the `call`
   flag.
4. **Combine allocations.** Consecutive allocations in a block with no
   safepoint between them merge into one `alloc` of the total size, followed
   by header and field stores at offsets.
5. **Build slow paths.** Each `has-slow` row gets a cold block. The cold blocks
   go after all hot ones in the final layout.
6. **Number safepoints.** Each `call`, `safepoint` and `preserving` instruction
   gets a safepoint id, whose map Allocate fills in.

The VM backend uses the same rows. Its templates are the `vm` field, so the
operations available to the VM are the ones with a VM opcode, and Select only
picks those when the target is `:vm`.

## 7. Allocate

**In:** LIR. **Out:** the same LIR with every vreg mapped to locations, extra
move instructions, a block order, a frame layout and safepoint maps.

### Data (native, linear scan)

- **Block order:** reverse postorder with each loop's blocks kept contiguous,
  cold blocks last. Instructions are numbered in steps of two in that order,
  so moves can be inserted at odd positions.
- **Liveness:** per block `live-in` and `live-out` bitsets over vregs, by the
  standard backward iteration. Loops settle in two or three rounds.
- **Intervals:** per vreg, a list of `[from, to)` ranges (with holes) and a
  list of use positions, each marked "must be in a register" or "may be in
  memory". Stored in columns: `iv-vreg`, `iv-ranges` (range into a shared
  column of pairs), `iv-uses`, `iv-reg`, `iv-slot`, `iv-parent` (for split
  children).
- **Fixed intervals:** one per physical register, covering the points where
  that register is taken: a `call` instruction takes them all, a fixed operand
  takes one.
- **Active and inactive lists**, and the unhandled queue sorted by start, as
  in Wimmer and Franz.
- **Stack slots:** a column of slots with a representation each (tagged or
  raw), reused once an interval ends.

### Data (VM)

None. The VM now gets its code from `compiler.c` (see The VM in the plan). The
first build's `allocate.ot` is where to borrow block liveness and parallel move
sequencing from.

### Output

- `li-loc` for each vreg occurrence: physical register, stack slot or VM slot.
- Resolution moves at block edges (block arguments, and split intervals that
  ended up in different places), as **parallel moves** sequenced with one
  scratch register for cycles.
- **Frame:** number of stack slots, outgoing area size, total frame size
  rounded to 16 bytes.
- **Safepoint maps:** for each safepoint id, a bitmap of stack slots holding
  live `tagged` values, and for `preserving` safepoints, a bitmap of registers
  holding them. Identical maps are interned so each is stored once.

## 8. Emit

**In:** allocated LIR. **Out:** a code blob.

### The assembler

An assembler is a record:

| field | contents |
| --- | --- |
| `bytes` | a growable bytevector |
| `length` | bytes written |
| `labels` | column: label id to offset, or unbound |
| `fixups` | column of `(offset, kind, label-or-slot)` |
| `returns` | column of `(offset, safepoint-id)` for return points |
| `positions` | column of `(offset, position, inline-index)` |

Encoders are one Otium file per target. Each function writes one instruction:
`(x64/add-rr asm dst src)`, `(x64/jcc asm cc label)`, `(a64/ldr-lit asm dst
slot)`. Register numbers are fixnums. Templates call encoders.

Fixup kinds:

- `rel8`, `rel32` (x86-64 branches and calls to labels within the unit)
- `imm19`, `imm26`, `imm14` (AArch64 conditional, unconditional, test-and-branch)
- `link` (a pc-relative load from the link table, resolved at install time)
- `abs-stub` (a call to a runtime stub, resolved at install time)

Branch sizes: backward branches use the short form when the target is in
range. Forward branches start long. A relaxation pass that shrinks forward
branches comes later if code size calls for it.

The C runtime needs to add bytevector accessors for 16-, 32- and 64-bit
little-endian loads and stores (`bytevector-u32-set!` and so on). Encoding one
byte at a time with `bytevector-u8-set!` would make the assembler several
times slower than it needs to be.

### Emission order

For each function in the unit:

1. Bind the function's label. Emit the **general entry**: argument count check,
   copying stack-passed arguments from the runtime state's argument area into
   the frame, optional defaults, rest list.
2. Bind the **direct entry** label. Emit the poll (stack limit compare) and
   frame allocation (`sub rsp, frame`).
3. Emit hot blocks in Allocate's order, each instruction's template, then the
   resolution moves on each edge. A jump to the next block in order is
   skipped.
4. Emit cold blocks.

After every function: resolve `rel*` and `imm*` fixups against labels. `link`
and `abs-stub` fixups are left for the installer.

### The code blob

What Emit hands to C, one Otium record:

| field | contents |
| --- | --- |
| `code` | bytevector of machine code |
| `fixups` | the unresolved `link` and `abs-stub` fixups, as a bytevector of packed `(offset u32, kind u8, index u32)` |
| `links` | array of link table entries: `(:const v)`, `(:cell id)`, `(:static-closure fn-index)`, `(:ic kind)` |
| `entries` | per function: general entry offset, direct entry offset, arity, name |
| `returns` | packed `(offset u32, frame-size u16, map u16)`, sorted by offset |
| `maps` | packed bitmaps |
| `positions` | packed `(offset u32, position u32, inline u16)` |
| `inline-tree` | packed rows |
| `deps` | the unit's dependency list |

The VM blob has the same shape with `code` holding 8-byte VM instructions and
no fixups.

## Installing code

This is the step that makes a blob executable. It's a C builtin,
`%code-install`, plus a small amount of Otium around it.

### Memory layout

C manages the code space in **chunks**, 1 MiB each. Every chunk has two halves
mapped next to each other:

```
chunk base                        base + 512 KiB
| code half (RX)                  | data half (RW)                  |
|  code object A  | code object B |  links A  |  links B  |          |
```

Code objects take space from the front of the code half, link tables from the
front of the data half. A code object's link table is always within 1 MiB of
its code, so:

- x86-64 reaches any link entry with `mov reg, [rip + disp32]`
- AArch64 reaches it with a single `ldr reg, literal` (range ±1 MiB)

A unit bigger than a chunk gets a chunk of its own, sized to fit. On AArch64
that may need `adrp` + `ldr`, which the encoder emits when the installer says
the chunk is large. That's two instructions instead of one, for a rare case.

W^X: on Linux the code half is mapped twice from one `memfd`, once RW (for the
installer) and once RX (for running), so code is never writable and executable
at the same address and no `mprotect` call is needed per install. On macOS the
chunk is `MAP_JIT`, and the installer toggles
`pthread_jit_write_protect_np` around its writes. Where neither works (some
locked-down systems), `mprotect` per chunk is the fallback.

### Steps

`(%code-install blob)` in C:

1. Reserve `code` bytes in a chunk's code half and the link table's size in its
   data half. Write the code bytes through the writable view.
2. Resolve `link` fixups: `disp = link_address(i) - (code_address + offset +
   instruction_end)`. Resolve `abs-stub` fixups against the runtime's stub
   addresses.
3. Fill the link table:
   - `:const`: the value. The collector traces these entries as part of the
     code object (see Reclaiming code), and updates them when it moves things.
   - `:cell`: the cell's address. Cells don't move.
   - `:static-closure`: allocate the closure now (its code address is known)
     and store it. Traced like constants.
   - `:ic`: initialize the cache to empty.
4. Flush the instruction cache for the code range (`__builtin___clear_cache`,
   a no-op on x86-64).
5. Allocate a **code object**, a small heap object of a new kind `K_CODE`,
   that points to a C `oti_code` struct:

   ```c
   typedef struct oti_code {
     uint8_t *start, *links;
     size_t size, link_count;
     uint32_t entry_count;
     oti_entry* entries;          /* general and direct offsets, arity, name */
     uint8_t* returns;            /* sorted return points */
     uint8_t* maps;
     uint8_t* positions;
     uint8_t* inline_tree;
     uint32_t* deps;              /* cell ids and versions */
     struct oti_code* next;       /* in the address index */
     bool marked;
   } oti_code;
   ```

6. Add it to the **address index**: a sorted array of `(start, end, code)`,
   searched by binary search when the stack walker has a return address.
7. Record the unit's dependencies against the cells, so a redefinition can
   find the code that relied on them.
8. Append a line to `/tmp/perf-<pid>.map` per entry when profiling is on.
9. Return the code object.

Back in Otium, the driver then:

- creates the closure for the unit's top-level thunk (always static) and calls
  it through `%code-call`, which enters native code through the C-to-Otium
  stub
- for `def` forms, the thunk itself stores closures into cells; storing a
  closure into a cell writes the cell's entry address

### Closures

A native closure is a heap object:

```
| header | code address (raw) | code object (traced) | free variable 0 | ... |
```

The raw address is what calls jump through. The traced code object reference
keeps the code alive. The collector skips the raw field because closures'
field layout says field 1 is opaque.

### Reclaiming code

This needs no general finalizers. No user code runs when code dies, nothing
can be resurrected, and the order things die in doesn't matter. What it needs
is three changes to the copying collector, all in `heap.c`:

1. **A code object's link table is traced as its fields.** When the collector
   copies a `K_CODE` object and later scans it, it also evacuates the
   constants and static closures in that object's link table, though they live
   out of line in the code space. Link tables are not roots on their own.
   That matters: a link table usually holds the unit's own static closures,
   which point back at the code object, so if link tables were roots, no code
   would ever die.
2. **Return addresses keep code alive.** The stack walker already looks up
   every return address in the address index to find its frame size and map.
   It also evacuates that code object, so a function that's been redefined
   while one of its frames is still running stays alive until the frame
   returns. Suspended green threads are walked the same way. A `try` record's
   resume address always belongs to a frame that's on some stack, so it needs
   nothing extra.
3. **A sweep of the code index after each collection.** Every `oti_code`
   points back at its `K_CODE` heap object. After copying, each entry is
   checked: if the heap object was forwarded, the code is live and the back
   pointer is updated to the new copy; if not, nothing reached it, and the
   code and its link table are freed. This is the same thing copying
   collectors do for weak references, and costs one check per code object.

The things that can refer to code are then all ordinary traced values or
stack frames:

- a closure, through its traced code object field
- a cell, through the closure it holds (the entry address only changes when
  the value does)
- an inline cache, through the closure or method it caches, stored in a link
  table
- a frame, through its return address

Freed space in a chunk goes on that chunk's free list, and a chunk with nothing
live is unmapped. Because every reference to code is traced, no stale return
address or cached entry can point at freed code.

This replaces `oti_reclaim` for native code. `oti_reclaim` is a separate mark
over the whole heap that can only run when no frames are live, at an
evaluation boundary. Manse's event loop and long-running green threads never
reach one. VM functions could move to the same scheme (a `K_CODE` object per
unit, with a frame's saved `pc` playing the role of a return address), which
would remove the evaluation boundary restriction for VM code too.

### Runtime stubs

The stubs are emitted by the backend at startup, as one special unit, and
installed like any other code. The installer is told this unit is the stub
unit, records each entry in `ot_state`, and uses them to resolve `abs-stub`
fixups from then on. Stubs:

- `enter`: C ABI in, Otium convention out. Saves C callee-saved registers,
  switches to the current Otium stack, loads dedicated registers, calls the
  closure, switches back, returns the result.
- `call-c`: Otium convention in, C ABI out, for runtime helpers. Saves the
  Otium stack pointer in `ot_state`, switches to the C stack, calls the helper.
- `preserve`: like `call-c`, but saves and restores every allocatable register
  into a save area in `ot_state`. Used by allocation slow paths, polls and
  arithmetic slow paths. The collector reads and updates tagged registers
  there using the safepoint's register map.
- `call-value`: the entry stored in a cell holding a callable collection,
  instance or record. Hands off to the runtime's existing call path.
- `unbound`: the entry of an unbound cell. Raises the unbound error with the
  cell's name.
- `vm-call`: the entry of a cell holding a VM closure. Enters the dispatch loop.
- `switch`: saves and restores an Otium stack pointer, for threads and
  one-shot continuations.

Before the stub unit exists, the runtime can only run VM code, which is the
bootstrap order anyway.

### Installing VM code

`%vm-install` takes the VM blob, creates `oti_function`s, interns constants
with `oti_constant` and attaches the safepoint maps. The VM's frames already
record their function and `pc`, so the walker looks up maps there instead of
in the address index.

## Testing

Most of the risk is miscompilation, so testing is layered. Each layer catches
bugs the others miss, and each one is cheap enough to run all the time.

### Printers, readers and verifiers

Every IR (Core, block IR, LIR) has a text form that prints and reads back,
round-tripping exactly. Unit tests for a pass are written as input IR text and
expected output IR text. This makes pass tests independent of the passes
before them, and makes failures readable.

A verifier per IR runs after every pass in test builds:

- **Core:** variables bound before use, `refs` and `sets` counts exact.
- **Block IR:** every value defined once; definitions dominate uses; block
  arguments match parameter counts; each block has one terminator; successor
  and predecessor lists agree; no function refers to another function's values
  after Closures; primop argument counts match the table; effect bits agree
  with the table.
- **LIR:** representations of operands match each row's spec; fixed operands
  in their registers; immediates in range.
- **After Allocate:** a **symbolic checker** in the style of Cranelift's
  regalloc2 checker. A forward dataflow over the allocated code tracks, for
  each register and stack slot, which vreg it holds. Every use must read a
  location that holds the vreg the instruction expects, on every path. Every
  safepoint map must list exactly the slots and registers holding live tagged
  vregs. This catches almost every allocator bug, without running anything.

### Reference interpreters

A Core interpreter and a block IR interpreter, both in Otium and both kept as
simple as possible. The Core interpreter is the executable form of the
semantics. Any program can be run:

1. by the simple compiler on the VM (the oracle)
2. by the Core interpreter, before and after Inline
3. by the block IR interpreter, after each of Convert, Propagate and Closures
4. by the optimizer on the VM
5. by the optimizer natively

Every one of these must print the same output, return the same value, and
raise the same condition type at the same source position. When they
disagree, the first stage that disagrees names the pass that's wrong. That's
the main reason for having interpreters at the IR boundaries, not just at the
ends.

### Operation-level checks

Small, exhaustive, and the place where the most bugs will be found early:

- **Folding agrees with the runtime.** For each primop with a `fold`, random
  and edge-case arguments (zero, ±1, fixnum limits, -0.0, NaN, infinities,
  wrong types) are folded and also passed to the real builtin. Results and
  errors must match.
- **Transfer functions are sound.** For each primop, random argument facts
  are drawn, then random concrete values inside those facts. The concrete
  result must lie inside the transfer function's result fact. This catches the
  classic range analysis bugs, such as an addition whose range wrapped.
- **Templates agree with each other.** For each LIR row, a generated function
  applies just that row to random arguments, compiled for the VM and natively,
  and both are compared with the builtin. Overflow boundaries and the slow
  path are always included.
- **Encoders agree with an assembler.** For each encoder function and random
  operands, compare the bytes with `llvm-mc` or GNU `as` output. A test-time
  dependency only.

### Fuzzing

- **Generated programs.** A generator of random Core programs, built to
  exercise the optimizer rather than the reader: nested lets and closures,
  `set!` on captured and uncaptured variables, named lets and `for` loops with
  bounded trip counts, fixnum arithmetic near the limits, floats, pairs,
  arrays, tables, `try` and `defer`, calls to known and unknown functions, and
  deliberately ill-typed operations so error paths are covered. Loops carry a
  fuel counter so every program terminates. The oracle is the Core
  interpreter. Failures are shrunk automatically by deleting and simplifying
  subtrees while the disagreement persists, then saved as regression tests.
- **Equivalence modulo inputs** (Le, Afshari and Su, 2014). Take any test
  program that has run once, find the code its input never reached, and mutate
  that code at random: delete it, add to it, swap it with other code. The
  output must not change. This finds optimizer bugs using the existing test
  suite as seeds, with no oracle needed beyond the original run.
- **Redefinition fuzzing.** Programs that redefine a global mid-run, at
  random points, with a function of a different arity or behaviour. Every call
  after the redefinition must see the new function, and code that inlined the
  old one must have been invalidated.
- **Chaos scheduling.** With green threads, a mode where every poll switches
  threads at random. The program's result must not depend on the schedule
  beyond what it explicitly allows.

Fuzzers run continuously on a spare machine and a few thousand programs run in
CI.

### Finding the faulty rewrite

Every rewrite in Inline, Propagate and Closures decrements `unit.fuel` and stops
rewriting when it hits zero. A failing program is rerun with fuel set by binary
search until the exact rewrite that breaks it is found, which is then printed
with the IR before and after. GCC's debug counters and LLVM's `opt-bisect` work
this way. Pass switches give a coarser version of the same search.

### The collector

- **GC at every allocation**, combined with **poisoning**: at each safepoint,
  every stack slot and saved register not in the map is overwritten with a
  value that traps when used as a pointer. A wrong map fails at once instead of
  silently keeping something alive or reading a stale pointer.
- The existing `OTIUM_CHECK_ROOTS` build unmaps the old semispace after each
  collection, so any pointer the collector missed faults on next use. It
  applies unchanged to native code.

### Proofs

A verified compiler in the style of CompCert or CakeML is out of reach for this
project, and the semantics it would need aren't written down yet. Three smaller
things are worth doing:

- **Specify Core precisely** (the semantics questions in `compiler-plan.md`),
  since every test above can only check against what's specified.
- **Check the algebraic rewrites with an SMT solver**, as Alive does for LLVM.
  Propagate's arithmetic simplifications (identities, comparisons against
  ranges, overflow-freedom conditions) are a few dozen small rules. Each can be
  stated over 63-bit integers and checked by Z3 once, at test time.
- **Translation validation for the allocator** is what the symbolic checker
  already is: it checks the output of each run instead of proving the
  allocator correct.

Anything beyond these should wait until fuzzing stops finding bugs.

### Performance as a test

Compile time per unit and instructions executed per benchmark (`perf stat -e
instructions:u`, which is stable where wall time isn't) are tracked per commit.
A change that makes the compiler or the generated code measurably slower fails
review the same way a wrong answer does.
