# Old C runtime leftovers

What's still open now that the new runtime is in `c/` and on main. Notes, not
a plan; add to it as found.

## Needs your edit

- README.md:37 says the C runtime has a generational collector; it is a
  semispace copying collector now.
- AGENTS.md says `make gc-lint`; the command is `just gc-lint`.
- `docs/how-to-run.md` doesn't list libffi as a dependency.

## Runtime gaps

- `flush-ports!` is documented for C in `lib/catalog/runtime.json` but unbound.
- otos prints errors as `error: MESSAGE`; it could use `ot_report_error`.
  Filesystem procedures (`file-exists?`, `file-info`, `list-directory`) fail
  on its embedded files, and QEMU exits 0 after `(exit 3)`.
- Diagnostics: a failing macro reports the inner error rather than E0112;
  builtin arity errors such as `map`'s have no code; `max`, `abs` and similar
  don't name the operator; E0202-E0208 don't use the catalog templates; a
  function from a `--load` file shows the script's path.
- `docs/language-guide.md` (generated) says the C runtime implements optional
  parameters with a rest-list fallback; not checked against the new runtime.
- fib30 is about 1.5% slower than at the start of 2026-10-27 (instruction
  count within 0.3%; looks like code layout). Other workloads are the same or
  faster.

## Tickets naming the old runtime

- `lan-ndec` (open): GC stress failures in ext-demo and conditions on the old
  runtime.
- `oti-lnk5` (open): prelude-failure abort, points at `c/src/otium.c` and
  `--c.max-depth`.
- `lan-roqx` (open): names `c/tests/test_multimethods.py`.
- `oti-6ybd` (in progress): the clean-room prototype ticket; the work has
  landed in `c/`.
- `lan-282o`, `lan-98nz` (closed): mention old paths in their history.
