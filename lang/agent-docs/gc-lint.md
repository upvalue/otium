# GC linter prototype

`tools/gc-lint` is an optional Clang-based checker for mistakes around Otium's
moving collector. It is separate from the runtime build and is only configured
or compiled when `make gc-lint` is requested.

The prototype currently checks one rule: a raw `ot_*_obj*` pointer must be
rederived after a call that may allocate. Rooting the corresponding `otv` keeps
the object alive and updates the `otv`; it does not update a C pointer derived
from the old value.

The checker derives `may allocate` summaries across direct calls within each
translation unit. Indirect calls are treated as possibly allocating. Public
allocation boundaries use `OT_GC_MAY_ALLOCATE` so extension translation units
receive the same information.

## macOS setup

Apple's Command Line Tools include a compiler, but not the Clang C++ development
headers used by LibTooling. Install Homebrew's keg-only LLVM package:

```sh
brew install llvm
make gc-lint
```

The runner asks `brew --prefix llvm` for the installation path, so Homebrew's
LLVM binaries do not need to be added to `PATH`. It can also use an
`llvm-config` already on `PATH`.

For a non-Homebrew install, set `LLVM_CONFIG` to the matching `llvm-config`
executable. The installation must include Clang's C++ headers and the monolithic
`libclang-cpp` library. On Linux these normally come from the distribution's
version-matched LLVM and Clang development packages.

LLVM is a system dependency for this optional tool. No LLVM source, library, or
submodule is stored in the Otium repository.

Otium itself does not acquire a Clang dependency. Normal Make targets do not
invoke this tool, the runtime does not link LLVM, and its public annotations
expand to nothing when a consumer compiles with GCC.

## Escape hatch

Some low-level code has guarantees the prototype cannot see. Put the escape
hatch on the raw pointer variable and include the reason:

```c
ot_string_obj* string OT_GC_ALLOW_STALE_BORROW("host pins this object") =
    (ot_string_obj*)ot_as_obj(value);
```

The annotation affects only stale-borrow diagnostics for that variable. It has
no runtime behavior and expands to nothing for non-Clang compilers. Run with
`--report-suppressions` to audit accepted escape hatches:

```sh
tools/gc-lint/run --report-suppressions
```

Do not use the annotation for ordinary rooted values. Rederive the pointer from
the rooted `otv` after the allocating call instead.

## Agent use

Run the checker after changing a function that retains a raw object pointer
while calling other runtime code:

```sh
make gc-lint
```

To make findings fail the command, use:

```sh
tools/gc-lint/run --warnings-as-errors
```

The runner builds and tests the checker before applying it to Otium's C source.
Generated headers and all checker build output stay under `build/`.

## Prototype limits

The current analysis propagates raw-pointer validity through Clang's control
flow graph. It is intraprocedural: allocation behavior is summarized across
calls, but raw-pointer and root state are not carried into callees.

Root-frame liveness, raw byte spans, write-barrier enforcement, bulk edge
copies, and tracer coverage are not implemented yet. The annotations and
standalone tool layout are intended to support those checks without adding the
tool to normal builds.
