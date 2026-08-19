# Runtime rewrite

A ground-up C implementation of the Otium runtime, replacing the previous one.
The organizing idea is containment: the language lives in one large file, and a
file earns its existence only by holding something we expect to swap out later.

Target spec is `agent-docs/spec.md` (draft 0.3). Otium runs on consumer PCs and
on capable embedded systems -- a couple MB of memory, something libc-esque
available, but nothing that leans on naive dynamic allocation. Every design
choice below defers to that.

## Files

```
Makefile           orchestration: objects, generated headers, test/bench targets
config.mk          defaults: CC, CFLAGS (-std=c23), feature toggles, heap defaults
site.mk            optional, gitignored, -include'd last for local overrides

src/otium.h        public API, plus internal object layouts behind an
                   OT_INTERNAL fence (the only header)
src/otium.c        the language: reader, printer, evaluator, namespaces,
                   conditions, collections, builtins, bootstrap
src/ot-gc.c        Cheney semispace collector, allocation, root frames, stats
src/ot-posix.c     platform layer: file I/O, clocks, signal-safe interrupt
src/main.c         the CLI: argument parsing, REPL (bestline), --server,
                   project.ot, load path

src/ot-ext-demo.c  one C file per native extension, linked into the CLI
src/ot-ext-ray.c   binary by default (ray only when raylib is available)

vendor/bestline/   vendored line editor, compiled into the CLI
```

That's the whole rule: `otium.c` holds the bulk; `ot-gc.c` exists because the
GC algorithm will likely be rewritten; `ot-posix.c` exists because embedded
targets replace it; `main.c` is the host program, not the runtime; extensions
get a file each. Nothing else.

The runtime library is `otium.o + ot-gc.o + ot-posix.o` (or a replacement
platform file). An embedder links that and never sees `main.c` or the
extensions.

There is exactly one header. Public consumers see the `ot_` API. The runtime's
own translation units define `OT_INTERNAL` before including it, which exposes a
clearly fenced second section: object layouts, heap functions, the accessors
the GC and the language core share. Nothing outside `src/` defines
`OT_INTERNAL`; a hygiene check enforces this.

House style:

```c
typedef struct ot_state { /* everything */ } ots;
typedef uintptr_t otv;
```

Public functions are prefixed `ot_`, statics are unprefixed.

## Build

Plain make. `make` builds `build/otium`; `make lib` builds `build/libotium.a`
(runtime only, no CLI, no extensions); `make test`, `make bench`, `make format`
do the obvious things. `config.mk` holds every default and is included first,
`site.mk` is `-include`'d for machine-local overrides (sanitizers, raylib
paths, a different CC).

The prelude is not optional: `tools/embed.py` generates
`build/gen/expander.h` and `build/gen/prelude.h` from `prelude/expander.scm`
and `prelude/prelude.scm`, and `ot_create` evaluates both before the `user`
namespace exists. A prelude-free runtime is not a supported build mode.

## Value representation

`otv` is one machine word:

- low bit `1`: immediate int, value in the upper bits (`v >> 1`). 63-bit on
  64-bit targets, 31-bit on 32-bit targets, which is exactly the spec's "at
  least 31 bits, wraps at the implementation width".
- low bits `00`: pointer into the GC heap. Objects are 8-aligned so this
  costs nothing.
- low bits `10`: small constants -- `nil`, `#t`, `#f`, `()`, plus internal
  sentinels (undefined, the unwind marker) -- encoded in the upper bits.

Floats are boxed heap objects. NaN-boxing is out because it dies on 32-bit
embedded, and a double doesn't fit an immediate there either. Float-heavy
loops will allocate; if that ever matters the fix is targeted caching in the
arithmetic ops, not a representation change.

Symbols and keywords are interned per-state; a symbol value is a pointer to
its intern record.

## Heap and GC

Every heap object starts with a one-word header: type tag plus payload size.
During collection the header doubles as the forwarding pointer, distinguished
by its low bit.

The critical constraint: language objects may not point at malloc'd memory.
Growable things use two-level heap structure instead. An `array` is a small
fixed object pointing at a `slots` backing object (an untyped value vector) on
the same heap; `string` and `buffer` point at a `bytes` backing object; a
`table` is a compact entries vector plus a hash index kept in a `bytes`
object. Growth allocates a bigger backing and repoints. Table deletion uses
tombstones, compacted when they dominate, which gives amortized O(1) delete
while preserving insertion order. Mutable objects used as table keys get a
stable id stamped at first use, so a moving collector never invalidates a
stored hash.

The collector is Cheney's algorithm over two semispaces: bump allocation in
to-space, copy from roots on exhaustion, grow toward the configured maximum if
a collection doesn't free enough. Both semispaces come from one reservation
made at `ot_create`; after that the runtime performs no dynamic allocation for
language objects. Host-side scaffolding (the printer's byte buffer, the CLI's
path handling) goes through a single allocator seam so an embedded host can
substitute its own.

Roots are: the frame chain (below), registered globals, and the state's own
value-typed fields (symbol table, namespace registry, cached common symbols,
handler and restart stacks, the in-flight condition), traced directly.

Extension values carry either an inline payload, which moves with the value,
or an external pointer with a finalizer. Finalizers for values that didn't
survive a collection run at the end of that collection; the collector keeps a
side list of live extension values so the sweep is O(live).

## Rooting

The GC is precise and moving, so any `otv` held in a C local across an
allocation must be registered:

```c
otv a = ot_nil, b = ot_nil;
OT_FRAME(S, &a, &b);        /* registers the addresses; the collector
                               updates them in place */
/* ... allocating calls; a and b stay valid ... */
OT_FRAME_POP(S);

OT_FRAME_SCOPED(S, &a, &b); /* same, popped by [[gnu::cleanup]] at scope
                               exit; compiler-specific and fine for now */
```

Implementation is a linked list of small structs built on the C stack by the
macro: `{ prev, count, otv *slots[N] }`. A few stores per frame, no dynamic
allocation, naturally re-entrant. `OT_GLOBAL(S, &g)` registers a long-lived
host root.

The house rule, enforced by a hygiene script: a raw `otv` never crosses an
allocating call unless its address is in a frame. During development the GC
has a stress mode that collects on every allocation, which turns rooting bugs
from heisenbugs into immediate failures.

## Interpreter

A tree-walking interpreter over the reader's output, replaced eventually, but
correct about the things that are semantic requirements now:

- **TCO** via a trampoline: `eval` is a loop, and every tail position (last
  body form, `if` branches, last of `and`/`or`/`let`/`begin`, the chosen
  `cond` clause, and tail calls into closures) rebinds `(form, env)` and
  continues instead of recursing. Non-tail evaluation recurses in C with a
  depth counter; exceeding the configured bound is a catchable error.
- **Environments** are heap objects (parent-linked scopes), so a closure is
  just `(params, body, env, ns)`.
- **Interruption**: the evaluator polls an interrupt flag at bounded
  intervals. `ot_interrupt` is async-signal-safe. A set flag unwinds with the
  quit condition, which handlers and `try` cannot intercept but
  `unwind-protect` cleanups run through.

Unwinding is cooperative, not longjmp: a raising operation returns a
distinguished sentinel and the condition lives in the state; callers propagate
with an `OT_TRY`-style macro. This is what makes handlers-before-unwinding,
restarts, `unwind-protect` on every exit path, and re-entrancy all compose.

Macro expansion stays out of C entirely. The expander is
`prelude/expander.scm`, written in the macro-free subset of the language; the
evaluator provides one oracle native (macro lookup by symbol) and calls the
expander on each top-level form before evaluating it. The C side only ever
sees special forms and applications.

`otium.c` is large by design. It's organized as banner-commented chapters in
dependency order: util (byte buffer, formatting), values, heap-layout
accessors, interning, reader, printer, equality and hashing, collections,
namespaces, conditions and params, evaluator, builtins, extension API,
lifecycle. The builtins chapter is the bulk by line count: one native per
spec entry plus a registration table.

## Re-entrancy and the embedding API

No mutable globals anywhere in the runtime. Everything hangs off `ots`;
multiple states coexist and are created and destroyed independently.

```c
ots *ot_create(const ot_config *);   void ot_destroy(ots *);
ot_config ot_config_default(void);   /* heap init/max, max depth */

void ot_set_writer(ots *, void (*)(void *ud, const char *, size_t), void *ud);
void ot_set_loader(ots *, bool (*)(void *ud, const char *ns,
                                   char **src, size_t *len), void *ud);
void ot_interrupt(ots *);

/* true: *out valid. false: inspect the condition via ot_condition(S). */
bool ot_eval_src(ots *, const char *src, size_t len, const char *name, otv *out);

void ot_def_nat(ots *, const char *name, ot_nat fn);
/* plus value constructors/accessors, extension types, gc stats */
```

C callables receive `(ots *, otv *args, int argc)` where `args` points into a
framed argument area, and return an `otv` or the unwind sentinel.

## CLI

`src/main.c`, using bestline for editing and history. Behavior:

- `otium FILE...` runs files in order; `otium` alone starts a REPL;
  `--repl` starts one after the files.
- `--server` runs the framed stdio protocol the conjure client
  (`vim/lua/otium/conjure.lua`) speaks: a request is source text terminated by
  a line containing US (0x1F); each response ends with RS (0x1E) followed by
  `ot> `. Any number of lines and top-level forms per request.
- The REPL detects incomplete input (unclosed forms) and continues the line
  rather than erroring, and reports every result including nil. On an
  unhandled condition it lists the active restarts by number and lets you
  pick one interactively -- this is the payoff of the condition system and
  the REPL should show it off.
- `(quit)` / `(exit)` leave the REPL or server cleanly. SIGINT sets the
  interrupt flag: in the REPL it cancels the current evaluation, in script
  mode it exits.
- Load path: `--path DIR` (repeatable), the `OTIUM_PATH` environment
  variable, then the nearest `project.ot` found by walking up from the
  working directory (`--project FILE` names one explicitly, `--no-project`
  skips the search). `project.ot` is read, never evaluated; `(paths ...)` is
  the only directive.
- Tuning: `--heap-init BYTES`, `--heap-max BYTES`, `--max-depth N`.
  Defaults: 1 MiB initial, 64 MiB max. The max is a backstop against runaway
  allocation, not a target; embedded hosts configure their own.
- `--gc-stats` prints collector statistics at exit: allocation and collection
  counts, bytes allocated, copied, reclaimed, used, peak, capacity.

## Extensions

Extensions are C files in `src/`, one per extension, linked into the `otium`
binary (never into `libotium`). No dynamic loading, no stable ABI yet.

An extension registers a module name. `(require 'demo)` creates and enters the
`demo` namespace, runs the C initializer once, restores the
caller's namespace, then looks for `demo.ot` on the load path -- the source
file is optional, useful for wrappers and constants that don't need
compiling.

The extension API is the same rooting discipline as the builtins, plus
extension types: `ot_ext_type` (name and optional finalizer),
inline payloads that move with the collector, pointer payloads owned by the
object, a checked accessor that raises an Otium condition on a wrong or
released value, and explicit release. Finalizers run during collection and
must not allocate on the Otium heap or re-enter evaluation.

`ot-ext-demo.c` is the dependency-free reference. `ot-ext-ray.c` is the
raylib binding, built when raylib is found (or forced off in `site.mk`); its
companion module and examples stay under `examples/ray/` as Otium source.

## Testing

Three layers:

- The conformance suite in `tests/otium/` (spec-driven, runner-per-file with
  expected output) stays as-is and is the primary target.
- A new CLI test script covering the flag surface, the server framing, REPL
  continuation, and exit codes -- written against this design, not inherited.
- A small C test binary for what scripts can't reach: re-entrancy (two
  states interleaved), frame discipline under GC stress mode, extension
  finalizer ordering.

Plus the hygiene script: no raw allocator calls outside the seam, no
`OT_INTERNAL` outside `src/`, formatting via `tools/format-c`.

## Milestones

1. **Skeleton.** Build system, value tagging, GC with frames and stress mode,
   interning, reader, printer. Checkpoint: read/print round-trips under GC
   torture.
2. **Evaluator.** Special forms, closures, TCO trampoline, namespaces and
   vars, minimal builtins. Checkpoint: fib runs; a deep tail loop runs in
   constant stack.
3. **Bootstrap.** Embedded expander and prelude, macro oracle, quasiquote.
   Checkpoint: macros work end to end.
4. **Conditions.** Handler and restart stacks, unwinding, `unwind-protect`,
   params, quit and interrupts. Checkpoint: the condition-system conformance
   tests pass.
5. **Library.** The full core library: ordered-table fine print, UTF-8
   strings, buffers, sequences. Checkpoint: full conformance suite green.
6. **CLI.** REPL, server, project files, load path, restart picker.
   Checkpoint: new CLI tests green; conjure works against `--server`.
7. **Extensions.** Foreign object API, port the demo extension, then the ray
   binding. Checkpoint: ray examples run.
8. **Measurement.** Benchmarks against the old numbers, `--gc-stats` review,
   heap-size tuning on a memory-constrained configuration.
