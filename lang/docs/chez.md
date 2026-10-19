# Otium on Chez

Chez is the default runtime: `./otium file.ot` runs `chez/run`. Its own tests
are in `chez/tests/` (`just test chez`). It uses the common reader, expander
and prelude; [portability](portability.md) covers the shared vocabulary and
known differences.

Native Chez bindings need (require chez), then the chez/ prefix. This is an
explicit host boundary, not a security sandbox. chez/import can introduce
native bindings into one namespace; implementation libraries choose their
own imports. Otium keywords and nil inside native syntax need quotation,
and Otium macros nested inside native syntax are not expanded.

Macros receive unevaluated datums and return one datum. They are unhygienic;
use gensym for introduced bindings. Transformer helpers resolve in their
defining namespace. Imported macros follow redefinition for subsequent
expansions; already compiled callers retain their expansion. Each outer
evaluation has a 10,000-invocation expansion budget, including nested loads
and eval. eval runs in the namespace where its binding was installed, without
capturing lexical variables. chez/load bypasses Otium processing.

The loader keeps namespaces for the lifetime of the process. Reload does not
restart frames, migrate state, watch files, or promise bounded memory
retention. Namespace calls follow redefinition; saved procedures do not.
(live 'binding) is a Chez convenience for a saved forwarding callback. A
portable forwarding fn provides the same behavior.

Conditions use Otium tables and arrays. Native exceptions are converted at
the Otium boundary; diagnostic wording remains native. Native continuations
restore dynamic restart bindings on reentry. Saved restart objects are
rejected outside their active extent. Use chez/values for native multiple
values; ordinary values enumerates a table.

quit, exit, and keyboard interrupts unwind cleanup and bypass ordinary
handlers/catches. Embedders receive an (otium conditions) control-condition;
control-kind distinguishes exit and interrupt, and control-status carries
the status. The runtime library does not terminate its embedding process.

The FFI and SDL bindings are shared with C; see [FFI](ffi.md) and
[SDL](sdl.md). REPL editing uses the common Bestline adapter described in
[REPL editing](repl.md).
