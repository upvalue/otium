# ✨ Lint and diagnostics

Lint catches likely mistakes before you run a program, including unfamiliar Lisp
spellings, unresolved names, and suspicious bindings. Error reports help you
find failures that occur while reading, expanding, or running code.

Both use diagnostics: messages with a severity, a code, and a source location.
Lint codes start with `L`; error codes start with `E`. A lint finding also names
its rule, such as `if-chain`, so you can configure or suppress it.

## Run lint

From the repository root, run `./otium lint` followed by the paths you want to
check. `just lint` checks `lib`, `examples`, and `benchmarks`.

Useful commands and options:

- `./otium lint --rules` lists rules and their default levels.
- `./otium explain L0201` explains the `unbound-name` rule.
- `--deny-warnings` makes warnings fail the command too.
- `--format=short` gives one-line reports; `--format=json` gives
  machine-readable output.

Rules have three levels: `allow` disables a rule, `warn` reports a warning, and
`deny` reports an error that fails the command. Warnings alone do not fail it by
default.

Lint checks names after macro expansion. This loads dependencies and runs
macros, so linting executes code. Use `--no-expand` for syntax-only checks
without loading dependencies. An `expansion-failed` warning means lint could not
run checks that need expansion on that form. Fix the reported expansion or
dependency problem before relying on its name checks.

## Act on a finding

Lint checks for:

- Otium syntax and name problems: foreign forms such as `define` or `lambda`,
  square brackets where binding lists belong, and unbound names. Use `def`,
  `fn`, and parenthesized binding lists; check spelling and imports.
- Suspicious code, including unused bindings, definitions that hide core names,
  and calls to a named `for` loop outside tail position.
- Readability and allocation issues: nested `if` chains, independent nested
  helpers, and rest parameters that allocate argument lists.
- Macro problems, including missing unquotes, accidental capture of caller
  names, namespace mistakes, and expansion-time state changes.

For example, `if-chain` suggests replacing an `if` in another `if`'s else
branch:

```otium
(let ((score 75))
  (if (>= score 90) :high
      (if (>= score 60) :pass :retry)))
;; => :pass
```

Use `cond` to put the choices at the same depth:

```otium
(let ((score 75))
  (cond
    ((>= score 90) :high)
    ((>= score 60) :pass)
    (else :retry)))
;; => :pass
```

For an unused binding, first check whether you forgot to use it. Remove it if
unnecessary, or start its name with `_` when it is intentionally unused, for
example, a callback parameter required by its caller.

## Suppress a warning deliberately

When the code is intentional, put a suppression comment immediately before the
form it covers. Name the rule and explain why it should not apply here:

```otium
;; lint: allow(if-chain) keeps the two decision stages separate
(let ((score 75))
  (if (>= score 90) :high
      (if (>= score 60) :pass :retry)))
;; => :pass
```

The comment covers the next form, including its contents. You can also place it
at the end of the form's first line.

For a whole file, use `;; lint: allow-file(if-chain) reason` before the first
form, replacing `reason` with a concrete explanation. Prefer a local suppression
when only one form needs an exception.

A missing reason or unknown rule is an error. A suppression that no longer
suppresses anything produces an `unused-suppression` warning; remove it so it
cannot hide future problems.

For command-wide policy, `--allow RULE`, `--warn RULE`, and `--deny RULE`
override project defaults. Use a source comment when future readers need to know
why a particular exception exists.

## Read an error report

Start with the header. For example, `error[E0401]: ` followed by
`` `car` expected a pair, got 5 `` identifies a type error and tells you which
operation rejected the value. Run `./otium explain E0401` for an explanation.

Then work through the report:

1. Find the location after `-->`. It is written as `path:line:column`, with
   lines and columns counted from one.
2. Read the source excerpt and carets. They mark the expression or token
   associated with the failure. For an unclosed form, the location points to the
   opening delimiter, not the end of the file.
3. Follow the notes. `in` names the innermost function. `called from` entries
   lead outward through callers, with locations where available. If `car`
   received a number, those callers can help you find where that number came
   from.
4. Check any help text. An unbound-name report may suggest a similar spelling or
   explain Otium's spelling of a familiar Lisp form.

When loading a dependency fails, the main location can be inside that
dependency. A `reached from` note points back to the source that caused it to
load.

Fix the reported cause, then run lint or the program again. For catching errors
and recovering inside a program, see [Errors and recovery](errors.md).

<!-- generated from lint.md.spec sha256:fca2c694429310a863cbb40eae02930a21d396418ab5fc69678f1d72eadbb0e9 -->
