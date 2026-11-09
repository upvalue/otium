# ✨ Lint and diagnostics

## Run lint

From the repository root, check selected files with:

```sh
./otium lint path/to/file.ot
```

Run `just lint` to check `lib`, `examples`, and `benchmarks`.

Lint reports each finding with a source location, code, and rule name. To list
the rules and their default levels, or get advice about a finding:

```sh
./otium lint --rules
./otium explain L0201
```

Deny-level findings appear as errors and fail the command. Warnings do not fail
it unless you add `--deny-warnings`:

```sh
./otium lint --deny-warnings path/to/file.ot
```

Name checks load dependencies and run macros during expansion. Use `--no-expand`
when you want syntax checks without loading those dependencies; name checks will
be skipped. If lint reports `expansion-failed`, fix that problem before relying
on the name checks for the affected form.

## Act on findings

Common findings include:

-  Lisp spellings that differ in Otium, such as `define` instead of `def`, or
  `lambda` instead of `fn`.
-  Square brackets used for bindings or parameters. Use parentheses; square
  brackets build arrays.
-  Unbound names, unused local bindings, and definitions that hide core names.
  An intentionally unused parameter can start with `_`.
-  An `if` in another `if`'s else branch, where `cond` would be clearer, or a
  nested helper that could move to a top-level `def-`.
-  Rest parameters that allocate argument lists. Use `&optional` for a bounded
  set of optional arguments.
- Calls to a named `for` loop outside tail position.
-  Macro mistakes such as missing unquotes, accidental capture, namespace
  references, and state changes during expansion.

Use the rule's explanation to decide whether to change the code. To change a
rule's level for a run, pass `--allow RULE`, `--warn RULE`, or `--deny RULE`.

## Suppress a finding with a reason

Put an `allow` comment before the form it covers. Use the rule name, not its
diagnostic code:

```otium
(def (f)
  ;; lint: allow(liftable-function) kept near its only caller
  (def (a) 1)
  (a))
```

The comment suppresses `liftable-function` for the following definition. Calling
`f` returns `1`.

You can also put the comment at the end of the form's first line. To suppress a
rule throughout a file, put `;; lint: allow-file(rule-name) reason` before the
first form.

Give a concrete reason for keeping the code. Missing reasons and unknown rule
names are errors. Lint also warns when a suppression suppresses nothing; remove
it when it is no longer needed.

## Read an error report

An error report starts with its severity, code, and message. For example:

```text
error[E0201]: unbound name `countr`
```

Read the rest of the report in this order:

1. Follow the `--> path:line:column` location. Lines and columns start at one.
2.  Look at the source line and the `^` underline to find the expression or name
   involved.
3.  Read any `note:` lines. For runtime calls, `in` identifies the innermost
   function, followed by `called from` locations working outward.
4.  Check `help:` for spelling suggestions or Otium equivalents of familiar Lisp
   names.

For an unclosed form, the location points to the opening delimiter that was
never closed.

Ask for a fuller explanation using the reported code:

```sh
./otium explain E0201
```

For editor or script integration, lint accepts `--format=short` for one-line
reports and `--format=json` for one JSON object per finding.

To handle errors within a program, see [Errors and recovery](errors.md).

<!-- generated from lint.md.spec sha256:ff67f0c798bbceb4522c8c91ff03c2c9b5bd6d544e3aaf0a6101e505336e8b5f -->
