# AMD64 assembly VM plan

Goal: replace the C dispatch loop (`dispatch` in `c/src/vm.c`) on x86-64 with
one written in assembly, the way LuaJIT's interpreter is written. It runs the
same bytecode, frames and heap objects. The C loop stays as the reference
implementation, for every other platform, and as the fallback for rare
instructions.

This plan comes from measuring the current VM and building a throwaway spike:
an assembly interpreter for 51 of the 68 opcodes that hands everything else
back to the C loop. All numbers are from one machine, a Ryzen AI Max+ 395
(Zen 5), with GCC 15.2 at `-O2` (the `debugoptimized` default) on Linux 6.18.
How they were taken is at the end.

## Summary

The C VM already runs about level with LuaJIT's interpreter (`-joff`) on fib
and a counting loop, and well ahead of Lua 5.4. Most of what LuaJIT's
interpreter gains over PUC Lua, the register VM has gained already.

On Zen 5 an interpreter's dispatch jump costs about 2.5 cycles whatever the
handler does. A handler of three instructions and one of nine run at the same
speed. Simple instructions are bound by that rate in C and in assembly alike,
so trimming their handlers, which is most of what hand-written assembly buys,
does nothing for them.

Assembly pays off in the long handlers: calls, returns, tail calls, builtin
calls and allocation. A call in particular is a chain of five dependent loads.

With two data-structure changes (each closure holding its function pointer at
a fixed place, and inline pair allocation), the spike used 18% fewer cycles
than today's VM on the 21 ports it runs almost entirely in assembly. The C VM
with the same two changes used 9.5% fewer. So about half the gain needs no
assembly, and assembly adds about 9% on top: nothing on tight loops, 20-25% on
the most call-heavy ports.

My estimate for a finished VM is 10-15% fewer cycles than an equally improved
C VM on VM-bound programs, and 5-8% over the R7RS ports as a whole, where GC,
the reader, ports and C builtins don't change. Against today's VM, roughly
double. This is one CPU; Intel cores need their own numbers.

## Where the time goes

`perf record -e cycles:u` over the 45 R7RS ports, with inputs scaled to run
0.3-3 seconds each:

- The dispatch loop, counting the `memmove` that `TAIL_CALL` calls, takes
  about two thirds of the cycles in the median port, and more than half in 32
  of the 45.
- GC takes over 20% in earley, graphs, nboyer and paraffins. Allocation
  (`oti_alloc`, `oti_heap_alloc`, `oti_float`) takes over 15% in deriv,
  diviter, divrec, fft, fibfp, mbrot, paraffins and sumfp. The float ports
  also spend 10-30% in `vm_float`, which GCC doesn't inline.
- equal, read1, string, sum1 and tail spend under 30% in the VM. Their time
  is in `equal?`, the reader, ports and string builtins, and no VM will
  change it.

Dynamic instruction mix, from a counting build, each port weighted equally:

| Instruction | Share |
| --- | --- |
| MOVE | 22.9% |
| CALL_GLOBAL | 8.1% |
| CDR | 5.9% |
| ADDI | 4.7% |
| CAR | 4.4% |
| CALL | 4.1% |
| GLOBAL | 4.1% |
| RETURN | 3.9% |
| BR_NULL | 3.4% |
| TAIL_CALL_GLOBAL | 3.4% |
| UPVALUE | 3.4% |

Per 100 instructions there are 9.7 calls to closures (3.8 of them tail
calls), 7.6 calls to builtins, 3.3 float results (each one a heap allocation
through `oti_alloc`) and 19.7 interrupt checks. `get` is a third of the
builtin calls: 2.4 per 100 on average, 12-15 in pnpoly, triangl, nucleic and
fft. The commonest pairs are MOVE then MOVE (6.6%) and MOVE then CALL_GLOBAL
(6.2%), both argument setup.

## What limits the C VM

### It's already at LuaJIT's interpreter

| | Otium C VM | LuaJIT 2.1 `-joff` | Lua 5.4.8 |
| --- | --- | --- | --- |
| fib(30) seven times | 635M cycles, 2.71G instructions | 691M, 2.29G | 997M, 4.69G |
| sum to 10^6 seven times | 109M, 575M | 102M, 442M | 126M, 785M |

Otium ran these through `c/bench/runtime.c` (`fib30`, `sum1000000`); the Lua
versions use local functions. LuaJIT's numbers are doubles. Otium executes
18-30% more instructions than LuaJIT, and takes 8% fewer cycles on fib and 7%
more on sum, which is the first sign that instruction count isn't what limits
it.

### The dispatch rate

A synthetic threaded loop, with bytecode cycling through five handlers, runs
at 2.53 cycles per dispatch whether the handlers are empty, do a register
move (four instructions) or a move with an add and overflow check (six), and
whether or not each reads what the previous one wrote. There are no
mispredictions. The shape of the jump doesn't matter: `jmp [table + op*8]`,
`mov` then `jmp reg`, and direct threading with the handler's address in the
instruction all measure 2.53. Direct taken jumps run at 0.52 cycles each.

The rate depends on the order of opcodes. It's 1.6 cycles when five handlers
take turns in a fixed order, 2.0 with two alternating, and 2.9 when every
dispatch goes to the same handler, as in a run of MOVEs.

The C VM agrees. Building `vm.c` with `-fcf-protection=none` drops the
`endbr64` Ubuntu's GCC puts at each handler, 3-6% of instructions, and cycles
stay within 1%. The sum port runs at 3.1 cycles per VM instruction in C. The
spike executes 21% fewer instructions there and takes 3-5% more cycles.

So one indirect jump per VM instruction is the floor, and simple instructions
sit on it. What can still go faster is executing fewer instructions per
program (fusing them) and the handlers long enough to stand above the floor.

### The call path

Hot paths per handler, dispatch included, counted with callgrind on a mix of
fib, tak, loops and list code:

| Handler | C | Spike |
| --- | --- | --- |
| MOVE | 11 | 7 |
| ADDI | 17 | 13 |
| CAR | 23 | 12 |
| BR_LTI | 19 | 14 |
| CALL_GLOBAL | 64 | about 50 |
| RETURN | 27 | 16 |

The first four don't matter, per the above. CALL_GLOBAL does. In order, it
reads `rt->globals` (GCC doesn't keep it in a register), loads the cell, checks
for the eval builtin and for unbound, checks the closure's kind, reads the
closure's field count to find the function id behind its captures, reads
`rt->functions[id]`, checks arity and `variadic`, checks stack room and frame
capacity, saves the caller's pc, writes the frame record (built in SSE
registers through a stack spill), updates `rt->frame_count`, `rt->max_frames`,
`rt->sp` and `rt->stack_roots.count`, and loads the interrupt flag pointer and
then the flag.

The cost is the chain: instruction, global cell, closure header, function id,
`rt->functions[id]`, `fn->code`, the callee's first instruction. That's five
dependent loads, roughly 20 cycles before the callee can read its operands. A
fib call costs about 34 cycles. On call-heavy ports, taking two loads out of
that chain did more than the rest of the spike: fib went to 0.92 of stock with
the spike alone and 0.77 with the closure change too.

### False leads

- `memmove` showed up as 37-83% of the cycles in ack, cpstak, takl and ntakl,
  called from TAIL_CALL to move two to four arguments, and in each port
  nearly all of it on one 16- or 32-byte vector load or store inside it.
  Replacing the call with a plain loop moved the time into the dispatch loop;
  the four ports changed by between +2% and -7%. The vector access was most
  likely waiting on the stores that had just written the arguments.
  Store-forwarding failures are still common in the C VM: 1,389M on the
  scaled takl against 749M for the spike, by `ls_bad_status2.stli_other`.
  Assembly controls store and load widths; C doesn't.
- Instruction counts. An IPC around 4 with almost no mispredictions looks
  instruction-bound, but here it's the dispatch rate and the call chain.
  Compare `cycles:u`, not `instructions:u`.

## The spike

`oti_asm_run(rt, stop_frames)` took the place of `dispatch` with the same
contract: return true at the stop depth, false on an error. It was 880 lines
of GNU as in Intel syntax, with handlers for moves, constants, GLOBAL,
upvalues and boxes, jumps, every fused branch, fixnum arithmetic and
comparisons, CAR, CDR and the predicates, CONS with inline bump allocation,
calls and tail calls to closures and builtins, and returns. Anything else,
including every float operation and CLOSURE, exited with the frame's pc at the
instruction. The C loop ran exactly that one instruction (a budget in its
`NEXT`) and the assembly was entered again. It used the existing frames,
function table and objects unchanged. All 45 ports gave the same output as
the stock build.

Cycles relative to the stock build, geomean over the 21 ports that hand back
fewer than 0.5 instructions per 100:

| Build | Relative cycles |
| --- | --- |
| C VM with a function pointer in closures and inline CONS ("fair C") | 0.905 |
| Spike | 0.902 |
| Spike with the same two changes | 0.821 |
| Spike with the changes, against fair C | 0.907 |

Some ports:

| Port | Fair C | Spike | Spike with changes |
| --- | --- | --- | --- |
| takl | 0.941 | 0.885 | 0.715 |
| nqueens | 0.954 | 0.759 | 0.743 |
| ack | 0.864 | 0.921 | 0.759 |
| fib | 0.810 | 0.922 | 0.769 |
| nboyer | 0.882 | 0.914 | 0.807 |
| tak | 0.940 | 0.882 | 0.838 |
| divrec | 0.726 | 0.694 | 0.602 |
| triangl | 1.025 | 1.049 | 0.968 |
| array1 | 1.036 | 1.029 | 1.028 |
| sum | 1.147 | 1.055 | 1.028 |

What to read into this:

- Fair C got 15% slower on sum from changes to other handlers. GCC lays out
  the whole dispatch function at once, and 5-15% swings on loops nobody
  touched are normal. Assembly doesn't have that problem, and it's a real cost
  of tuning the C loop.
- The builtin-heavy ports (array1, triangl, bv2string) gained little or lost.
  The likely reason is that the spike reloads all frame state after every
  builtin call, about 35 instructions; I didn't confirm it. A finished VM
  checks whether the stack moved instead.
- Float-heavy ports, cpstak and wc got much slower. They hand back to C up to
  49 times per 100 instructions (mbrot), and each hand-back costs 20-30 cycles
  more than running the instruction in place. That's the cost of the spike's
  fallback.

## What to expect

The spike's 9% over fair C should grow in a finished VM. It reloads everything
after builtin calls, runs floats, closures, boxes and slow calls in C, and
wasn't tuned. With those done I'd expect 10-15% over an equally improved C VM
on VM-bound programs, more on call-heavy ones, and close to nothing on tight
arithmetic loops, which sit on the dispatch floor in both.

Over the R7RS ports as a whole it's less, since a third of them spend most of
their time in GC, the reader, ports or C builtins. Weighting by the median VM
share of two thirds gives 5-8% from assembly, and 10-15% against today's VM
once the C-side changes are counted.

Two caveats. These are Zen 5 measurements, and its indirect branch behavior
is what sets the floor; an Intel core needs its own numbers before asm becomes
the default. And the macOS build here is aarch64, which gets nothing from this
plan except the C-side changes.

## Design

### Shape

- Same bytecode, frames, functions and objects as the C VM. compiler.c, the
  optimizer, `%vm-install`, the disassembler, code reclamation and error
  traces don't change, apart from the data-structure changes below, which
  both loops get.
- One file, `c/src/vm_x64.S`: GNU as, Intel syntax, gas macros for the
  families (the ten fused compare branches are two macros). Not DynASM:
  nothing needs emitting at runtime, and plain assembly works with gdb, perf
  and objdump as is. Each handler gets a local symbol (`op_MOVE`), which makes
  `perf report` a per-opcode profile for free.
- Offsets come from the C headers at build time: compile a small C file of
  `offsetof`s to assembly and pull out `.equ` lines, the way Linux makes
  asm-offsets, so cross builds work. `_Static_assert`s in `internal.h` cover
  what the assembly assumes about shapes, such as the 32-byte frame record and
  the object header layout.
- `oti_vm_run(rt, stop_frames)` has `dispatch`'s contract, and `oti_drive`
  calls it in place of `dispatch`. `OTIUM_VM=c` in the environment, read at
  `ot_create`, selects the C loop in an asm build, for bisecting and for
  differential tests.
- A `vm` meson option (`c` or `asm`) next to `vm_switch`. `vm_x64.S` stays out
  of `sources.list`, so otos and the amalgamation keep the C loop.

### Three ways an instruction runs

1. In assembly: the fast paths of everything common.
2. In assembly calling C: builtins, `oti_apply` for the slow calls (rest and
   optional parameters, generics, records, instances, collections, `apply`),
   allocation when the heap is full, arithmetic slow paths through the
   builtins, and closure creation to start with.
3. Handed back: the assembly stops before the instruction, the C loop runs
   exactly one instruction, and the assembly carries on. TRY, CLEANUP,
   RESTARTS and the other dynamic-state instructions, SET_GLOBAL and
   DEFINE_GLOBAL can stay this way.

Hand-back is also how the VM gets built. Start with every instruction handed
back and the whole test suite passing, then move instructions into assembly
one at a time with the suite green throughout. The C loop's budget check costs
a compare per instruction, so compile it only into asm builds.

### Registers

```
rbx   PC       next instruction; the running one is at PC-8
r15   BASE     &stack[frame.base], register 0
r14   FRAME    the running frame's oti_frame
r13   CODE     fn->code, for jump targets
r12   RT       ot_state*, which also holds the dispatch table
rbp   SP       stack height, as an index
r11   GLOBALS  rt->globals
rax rcx rdx rsi rdi r8 r9 r10   scratch
```

The six callee-saved registers hold the state that survives a C call. GLOBALS
is caller-saved and reloaded after every C call, since a call can define
globals and move the array. The function keeps a fixed C stack frame for its
spill slots (the stack and frame arrays as last seen, the stop frame, the
frame high-water mark), so rsp is 16-byte aligned at every call with no
pushes in handlers. The spike kept the dispatch table in r10; moving it into
`ot_state` is untested.

### Dispatch and operands

```
movzx eax, byte ptr [rbx]
add   rbx, 8
jmp   qword ptr [r12 + rax*8 + RT_DISPATCH]
```

Every handler ends with its own copy. Operands are read where they're used,
relative to PC: `a` at `[rbx-6]`, `b` at `[rbx-4]`, `c` at `[rbx-2]`, `bc` as
a dword at `[rbx-4]` and `x` at `[rbx-7]`. The format stays 8 bytes, since
re-encoding it buys nothing measurable here (direct threading measured the
same).

With the table in `ot_state` it needs no register, and it can be swapped per
runtime. A table of counting or tracing handlers gives instruction statistics,
single-stepping for Manse or a profiler, at no cost when it's off. LuaJIT does
its hooks this way.

### Calling C

Before the call, store PC in the frame record (error locations come from it
too), write SP to `rt->sp` and `rt->stack_roots.count`, the frame count from
FRAME, and `rt->max_frames` from the high-water mark.

After it, a zero result or `rt->error.set` exits with false, as the C loop
does. If `rt->stack` moved, rebase BASE. Reload GLOBALS. If `rt->frames` moved
or the frame count changed (`oti_apply` entered a closure), reload everything
from the top frame, as C's `RELOAD` does.

Between C calls, `rt->sp` and `rt->frame_count` are stale on purpose. Only C
reads them, and C only runs when the assembly calls it. The spike synced them
the same way but reloaded everything after every call, which is the likely
reason builtin-heavy ports got slower.

### Garbage collection

The collector only runs inside C calls, so the rules are about what crosses
one:

- No managed value stays in a machine register across a C call. Read operands
  again from the VM registers afterwards. For CONS the spike allocates first
  and reads `rB` and `rC` after, which is right on both the inline and the
  slow path.
- BASE is rebuilt after any call that may grow the stack.
- `stack_stale` works as in the C loop: a call whose frame reaches above both
  SP and `stack_stale` sets the new slots to unbound before anything runs. The
  spike reads and writes `rt->stack_stale` directly.
- gc-lint can't see assembly. The `check-roots` build, which moves the heap and
  the VM stack at every allocation and stack request, is what catches a missed
  reload, and it has to pass with the asm VM on.

### Errors, interrupts and threads

- Errors save PC, sync and return false, and `oti_drive` unwinds and enters
  again, as now. Frame pcs must match what the C loop saves at the same
  points, or error locations move.
- Interrupts are checked at calls and backward jumps as in C, calling
  `oti_interrupt_point` when the flag is set.
- Builtins call back into the VM through `oti_call_list`, which enters the
  assembly again. It keeps no global state, so that works as it does in C.
- Stack switching for green threads (`threading-plan.md`) and preemption
  happen inside C calls. If they arrive, anything that swaps stacks should
  bump a counter that the after-call check also compares.

### Live reload

Unchanged. Global calls read the cell at every call, closures point at their
own function, and code is only reclaimed when no frames are live. The assembly
caches nothing across a C call that redefinition could change.

## Changes to land in C first

These help both loops, and doing them in C first keeps the comparison honest.

1. **Function pointer in closures.** Field 0 of every closure holds its
   `oti_function*` tagged as a fixnum (pointer | 1), so the collector leaves
   it alone, and captures move up one field. The function id stays in the
   opaque bytes for code reclamation. This touches the four closure allocation
   sites (`oti_execute`, `vm_closure`, `oti_evaluator` and the optimizer's
   static closures) and the three upvalue instructions. Fair C, which had this
   and inline CONS, was 19% faster on fib and 14% on ack. A later step could
   put the arity, frame size and a back pointer in front of the code array, as
   LuaJIT does, so the closure holds the code pointer and the call chain loses
   one more load.
2. **Inline allocation.** A shared fast path in `heap.h` for small objects:
   bump the pointer when there's room and nothing needs the slow path. Give
   the heap one flag that sends everything to the slow path (GC stress, host
   I/O), so the check is a single load. Use it for CONS, BOX, CLOSURE and
   float results. diviter, which is mostly CONS in a loop, was 27% faster in
   fair C.
3. **Inline `vm_float`.** `always_inline` alone makes fibfp, mbrot and sumfp
   19-20% faster, pnpoly 10% and fft 6%.
4. **A non-null interrupt flag.** Point `rt->interrupt_flag` at a zero word
   when the host hasn't set one, so a check is one load and a compare.
5. **Branch flags are 0 or 1.** The fused branches compare the test result
   with `x`, and the installer accepts any byte today. Reject other values in
   `%vm-install` and assert in compiler.c.

## Stages

Each stage leaves the full test suite passing with both loops.

**0. Baseline.** Check in the scaled inputs and a script that runs them with
`perf stat -r 3 -e cycles:u` on pinned cores and reports geomeans for the
VM-bound group and the whole set. Land the C changes above and record what
each gave.

**1. Skeleton.** The `vm` option, offsets generation, `oti_vm_run` with entry,
exit, sync and reload, and the C loop's single-step mode. First with every
instruction handed back, then moves, constants, globals, upvalues, boxes,
jumps and fused branches. This stage tests the protocol; expect no speedup.

**2. Calls.** CALL, TAIL_CALL, the GLOBAL forms and RETURN, builtin calls with
the after-call checks above, `oti_apply` as a C call, and interrupt checks.
Gate: a geomean of at least 10% fewer cycles than fair C on the call-heavy
VM-bound ports (fib, tak, takl, ntakl, ack, nboyer, sboyer, mazefun, conform,
browse, mperm). The spike got 11% there, from 5% on fib to 24% on takl. If a
finished call path doesn't beat the spike, stop and find out why before going
further.

**3. Arithmetic and allocation.** Fixnum and float paths for ADD, SUB, MUL,
ADDI and the comparisons, matching C exactly: overflow goes to the builtin,
float addition starts from 0.0 so `(+ -0.0 -0.0)` is 0.0, and NaN compares
false. Inline CONS, BOX, CLOSURE and float boxing. Fast paths for the hottest
builtins at the call site: when the callee is the `get` or `put!` builtin and
the target is an array with a fixnum index in range, do the access inline.
Checking the value rather than the cell keeps redefinition working.

**4. Tuning.** Store-forwarding failures (a header written as one 8-byte store
and read back a byte at a time is a candidate), alignment, the frame
high-water bookkeeping, and whatever is left on the hand-back list
(`OTIUM_VM_STATS`).

**5. Fewer dispatches.** Simple instructions sit on the dispatch floor, so
executing fewer of them is what's left, for both loops. Candidates from the
pair counts: calls that name their argument registers in a trailing word in
place of the MOVEs before them (MOVE then MOVE and MOVE then CALL_GLOBAL are
13% of pairs), a two-move instruction for loop updates, and the optimizer
computing arguments into call windows directly (the "call windows" gap in
`docs/optimizer.md`). New instructions go through compiler.c, the optimizer's
opcode table and `%vm-install`, so plan them with that work. A new instruction
can ship in the C loop first and be handed back until it gets a handler.

For size: the spike is 880 lines for 51 opcodes, without floats, closures or
most slow paths. A finished VM is probably 2,500-3,500 lines of assembly and a
few hundred of C.

## Testing

- `just test` with the asm VM, and again with `OTIUM_VM=c`.
- The R7RS ports with reduced inputs (`just test bench`) and with the scaled
  inputs, comparing output between the two loops.
- `just test-stress`: inline allocation has to take the slow path under GC
  stress.
- `just check-roots`, which moves the heap and stack at every allocation and
  stack request. This is the test that catches a stale BASE.
- `just sanitize` covers the C side. ASan doesn't instrument the assembly.
- Unit tests for the edges the fast paths own: fixnum overflow at both ends
  for each arithmetic instruction, `-0.0`, NaN in every comparison and both
  branch polarities, tail calls with no arguments and with 255, calls that
  grow the stack and the frame array mid-run, interrupts in a tight loop and
  at a call, errors inside builtins and inside nested callbacks, the stack and
  frame limits (with the same messages), CLEAR of zero registers, JBOUND,
  RESULT after a restart, and the eval builtin read through GLOBAL.
- `OTIUM_VM_STATS=1` prints how many of each instruction ran in C, as a
  coverage report.

## Risks and open questions

- **One CPU.** The dispatch floor and the call chain's cost are Zen 5
  numbers. Measure on an Intel core before making asm the default.
- **Two loops to keep in step.** Every instruction's semantics live in two
  places. Differential tests are the defense, and hand-back keeps a new
  instruction cheap to add.
- **Review.** Assembly is harder to review than C and invisible to gc-lint.
  The register and GC rules belong at the top of the file.
- **Apple Silicon.** The aarch64 macOS build gets the C changes and nothing
  else. An AArch64 port of the same design would be its own project.
- **Other x86-64 targets.** macOS on Intel needs Mach-O spellings (symbol
  prefixes, sections), and Windows needs a different set of callee-saved
  registers. Neither is planned.
- **CET.** Give the object a `.note.GNU-stack` section, and mark it as
  shadow-stack compatible (its calls and returns are balanced) but not IBT, or
  every handler needs an `endbr64`. With no property note the linker drops the
  CET marking for the whole binary.
- **Manse.** Class-heavy code goes through generic dispatch and instance calls,
  which stay in C either way. Measure Manse on its own before expecting the
  R7RS numbers there.

## After this

An interpreter can't get under one indirect jump per instruction. Past this
point the gains come from removing dispatch: a template or copy-and-patch
compiler that stitches these handlers together per function, which fixed
register conventions make possible, or the native backend in
`native-compiler-plan.md`. A template compiler should use this VM's frame and
call conventions, so calls between the two stay cheap. The native plan doesn't:
it gives native code its own convention (arguments in registers, hardware
`call` and `ret`) and only meets VM code through transition stubs, during
bootstrap and on platforms without a backend.

Floats are boxed, and every float result allocates. Inline boxing makes that
cheaper; unboxed floats (NaN-boxing, or float registers from the optimizer)
are the larger change and outside this plan.

## Appendix: how the numbers were taken

- Cycles: `taskset -c N perf stat -r 3 -x, -e cycles:u,instructions:u`, six
  ports at a time on separate physical cores. Builds were stock
  `debugoptimized` with `-Dui=disabled`; the variants replaced `vm.c` (and for
  the closure change `compiler.c` and `optimizer.c`) and relinked.
- Profiles: `perf record -e cycles:u` and `perf report --sort symbol`.
  Top-down: `perf stat -M PipelineL1,PipelineL2`. IBS needs system-wide perf,
  which wasn't available.
- Instruction mix and builtin calls: a build of `vm.c` with counters in `NEXT`
  and in the builtin call path.
- Handler paths: `valgrind --tool=callgrind --dump-instr=yes`, walking from
  each handler's entry along the more-executed successor to the dispatch jump.
- Dispatch rate: a standalone assembly loop, 20 bytecodes of 8 bytes cycling
  through five handlers, 200M dispatches.

Scaled inputs, as the first values of each port's input. The rest is the
vendored input. Ports whose sizes changed report INCORRECT because the
expected answers weren't updated; the work done is the same.

| Port | Input | Port | Input |
| --- | --- | --- | --- |
| ack | 40 3 8 | mperm | 10 9 2 1 |
| array1 | 50 1000000 | nboyer | 1 3 |
| browse | 200 | nqueens | 1 13 |
| bv2string | 100 | ntakl | 1, lists 32..1, 16..1, 8..1 |
| cat | 5 | nucleic | 5 |
| conform | 50 | paraffins | 10 20 |
| cpstak | 1 32 16 8 | peval | 200 |
| deriv | 1000000 | pnpoly | 100000 |
| destruc | 400 600 50 | primes | 1000 1000 |
| diviter | 100000 1000 | quicksort | 250 10000 |
| divrec | 100000 1000 | ray | 5 |
| earley | 1 12 | read1 | 250 |
| equal | 100 (unchanged) | sboyer | 1 3 |
| fft | 10 65536 | simplex | 100000 |
| fib | 3 32 | string | 10 500000 |
| fibfp | 1 35.0 | sum | 20000 10000 |
| gcbench | 1 16 | sum1 | 25 |
| graphs | 1 6 | sumfp | 50 1e6 |
| lattice | 1 44 | tail | 50 |
| matrix | 250 5 5 | tak | 1 32 16 8 |
| mazefun | 1000 11 11 | takl | 1, lists 32..1, 16..1, 8..1 |
| mbrot | 100 75 | triangl | 5 22 1 |
| | | wc | 5 |
