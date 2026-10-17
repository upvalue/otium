# REPL editing

Run `./otium` or `./otium-chez` in a terminal. Both use the same native line
editor in `shared/`, based on Bestline. Chez builds its small shared library
on the first interactive launch; a C compiler and make are required then.
Script execution and redirected input do not need that library.

Use arrows to move or recall history, Ctrl-A / Ctrl-E for the start / end of
the line, Ctrl-R to search history, and Ctrl-U / Ctrl-K to erase before / after
the cursor. Ctrl-C discards unfinished input. Ctrl-D on an empty line exits.
Each runtime still reads and evaluates its own dialect.

Press Ctrl-X followed by Ctrl-E to open the current editable input in an
external editor. The command comes from `$VISUAL`, then `$EDITOR`, then `vi`.
Commands with arguments work, for example `EDITOR='code --wait'`; the editor
must wait until editing finishes. The temporary file is removed afterward.
On success, the edited text returns to the prompt. Press Enter to evaluate it,
or keep editing. An editor that exits unsuccessfully leaves the original
input in place. Embedded newlines are preserved.

At a continuation prompt, the editor receives the current line, not earlier
lines already submitted to the runtime reader. To compose a whole multiline
form externally, open the editor at a fresh prompt.

Both runtimes share `~/.otium_history`. Set `OTIUM_HISTORY` to an absolute file
path to use a separate history, or to an empty string to disable history.
Multiline entries survive a restart. Existing single-line history files are
still readable. History is reloaded at each prompt so the runtimes can pick
up each other's entries; simultaneous writes are not locked.

`make test-repl` exercises both launchers through pseudo-terminals. It uses a
fake editor and temporary history files, without changing your history.

Tab completion and help

Tab completes the name ending at the cursor. Repeated Tab cycles through
matching names in alphabetical order. The rest of the line is preserved;
completion stays out of strings and line comments. Completion covers the
current namespace's definitions, imports, and core bindings, including
qualified imports such as counter/tick!. It refreshes after evaluation and
reload. It does not infer local bindings inside an unfinished expression or
complete filenames.

At a fresh prompt, type `,help` for controls, `,help NAME` for documentation,
or `,help PREFIX` to list matching names. Lookup reads a snapshot of binding
metadata; it does not evaluate an expression or call the selected function.
Documentation comes first from Otium definition docstrings. Functions, macros,
and values accept an optional leading string:

    (def (square x) "Square the input." (* x x))
    (def limit "Maximum number of items." 100)
    (defmacro twice (x) "Evaluate x twice and add the results." `(+ ,x ,x))

A string is documentation only when another expression follows it. Thus
`(def greeting "hello")` still defines a string value. Docstrings are
metadata and are removed before evaluating the definition body. Redefining a
binding replaces its documentation; defining it without a docstring clears
its old documentation. `defonce` preserves both value and docs on reload.

`(describe 'square)` retrieves documentation in scripts as well as the REPL.
`describe` and `,help` use the same binding lookup. Imports read documentation
from their defining namespace. `describe` returns the namespace/name followed by the docstring, or
`No documentation recorded.` when the binding has no documentation. Unknown
bindings return `nil` on both runtimes.

Runtime-provided Otium functions and forms use the shared reference in
`shared/documentation/runtime.json`. Entries declare which runtime implements
them, with runtime notes where behavior differs. Otium prelude functions and
macros keep their docs beside their definitions, rather than duplicating them
in the reference. User definitions take precedence over runtime entries.
Chez-native names are available with a `chez/` prefix for completion, without
a bundled Chez manual or fallback descriptions.

After editing the runtime reference, run:

    tools/python shared/documentation/generate.py

The generated C and Scheme tables are checked in; normal builds require no
generation step or network access. `make test-repl` verifies they match the
shared source and that public shared prelude definitions carry docstrings.

For example, in either REPL:

    (def (square x) "Square the input." (* x x))
    ,help square

Then type `(squ` and press Tab. Keep typing the argument and closing
parenthesis before pressing Enter. Commands are available in terminal REPLs;
script files continue to use their runtime's reader.
