# Command-line programs

Use `./otium` for either runtime. Chez is the default; `--runtime=c` selects
C. The selector is consumed before `--`; after it, the same token is an
application argument.

Both runtimes accept the same launcher syntax:

```sh
./otium --runtime=c --c.heap-max=67108864 program.ot -- --count 3 Alice
./otium --runtime=chez --c.heap-max=67108864 program.ot -- --count 3 Alice
```

The first `--` separates launcher arguments from application arguments. The
launcher consumes it. Everything following it reaches the application unchanged,
including empty strings, dash-prefixed strings, and further `--` tokens. Without
the separator the script gets an empty argument list. Exactly one positional
script is allowed before the separator. Use `./-name.ot` for a dash-prefixed
script filename. A separator without a script is an error.

`--help` before the separator prints launcher help without executing the script.
After the separator it belongs to the application. No script starts the REPL;
`--repl` enters it after loading a script. `--load FILE` loads a file before the
script, and can be repeated. A load-only invocation finishes without a REPL.

Both launchers support `--path DIR` (`-L`), `--project FILE`, `--no-project`,
`--load FILE`, `--repl`, and `--help` (`-h`). Module directories are searched in
this order: explicit paths, nonempty `OTIUM_PATH` entries separated by `:`, then
project paths. Project discovery searches parent directories for `project.ot`.
Relative project paths are relative to the project file. If there are no paths,
the search defaults to `.`. As in the existing C launcher, a missing project
file contributes no paths. Project files provide quoted path strings, not code
to evaluate. `--project` and `--no-project` conflict.

Runtime-specific options have an owner prefix. C supports `--c.heap-init=BYTES`,
`--c.heap-max=BYTES`, `--c.max-depth=N`, `--c.gc-stats`, `--c.gc-stats-json`,
`--c.gc-force-compact`, and `--c.server`. Chez accepts these and ignores them
silently. Both validate names and individual value syntax and bounds, so a typo
is an error even on the inactive runtime. Cross-option heap constraints apply
only in C. Numeric backend options require `=VALUE`. Unprefixed C-specific
spellings have been removed. There are no Chez-specific options yet.

The option catalog is `lib/catalog/options.json`. Run
`just generate` after editing it; the `generated` test checks the C and Chez
catalogs. Launcher usage errors exit with status 2;
script failures exit with status 1.

## Process arguments

```scheme
(require otium.process)
(otium.process/arguments)   ; list of strings, excluding the script name
(otium.process/script-name) ; script filename, or #f without a script
```

These accessors work the same way on C and Chez. Embedders start with no script
and an empty list. C hosts can supply them with `ot_set_arguments`; the runtime
copies the strings. `otium.process/write-error` writes a string to stderr.

## Parsing application arguments

`otium.cli` is a shared Otium implementation with small runtime adapters. Load
it with `(require otium.cli)`. This unaliased require form works in both dialects.
See `examples/greet.ot` for a complete program:

```scheme
(def command
  (otium.cli/compile
    '(command greet
       (summary "Greet someone.")
       (version "0.1.0")
       (argument name (help "Person to greet"))
       (option count (names "-n" "--count")
         (type integer) (range 1 100) (default 1)
         (metavar "N") (help "Number of greetings")))))

(def (main values)
  (display (otium.cli/ref values 'name))
  (newline)
  0)

(otium.cli/run command main)
```

`compile` validates a command declaration. Declarations are ordinary lists;
construct them with `list` to include converter procedures. Replacing a command
binding replaces its specification without a registry. A converter wrapper can
look up a redefined namespace function when called.

A command contains `summary`, `version`, `examples`, argument declarations, and
nested `command` declarations. Put positional arguments on leaf commands.
Subcommand selection is required when a command declares children.

| Declaration | Behavior |
| --- | --- |
| `(argument name ...)` | Required positional, unless a default or `(required #f)` is supplied |
| `(option name (names "-n" "--name") ...)` | One value per occurrence |
| `(flag name (names "-q" "--quiet") ...)` | Boolean, false by default |
| `(counter name (names "-v") ...)` | Counts occurrences, starting at zero |

Options accept separate values, `--name=value`, and `-nVALUE`. Short flags can
be clustered (`-vvq`). Long-option abbreviation is disabled. `--` ends application
option parsing. Negative numeric positionals are accepted; ambiguous strings
can follow `--` or use an attached option value.

Attributes include:

- `(type string)`, `path`, `integer`, `number`, or a converter procedure.
  Paths remain strings. Integers use signed decimal syntax and the shared signed
  63-bit range. Numbers use finite decimal floating-point syntax, including
  exponents; Scheme fractions, complex numbers, and reader prefixes are rejected. Conversion failures become parse errors; exceptions from custom
  converters propagate to the caller.
- `(default value)`, `(required #t)`, `(range low high)`, and `(choices value ...)`.
  Defaults are already converted application values.
- `(repeat append)` collects values in occurrence order; `(repeat last)` replaces
  earlier values. Otherwise repeated scalar arguments are errors. A positional
  with `append` must be last and requires at least one value unless optional.
- `(negative "--no-color")` supplies an explicit negative flag spelling.
- `(global #t)` makes an option available in descendant commands.
- `(conflicts other ...)` and `(requires other ...)` constrain explicitly supplied
  arguments. Defaults do not count as supplied for these checks.
- `(help "Description")` and `(metavar "VALUE")` control generated help.

`(examples ("greet Alice" "A greeting") ...)` adds examples to help.
Help displays descriptions, defaults, choices, ranges, subcommands, and inherited
options. It bypasses required-argument validation. Formatting is plain text and
deterministic; terminal-width wrapping, color, completion, environment defaults,
and a command-definition macro are not included.

## Parsing without exiting

`(otium.cli/parse command '("-n3" "Alice"))` returns an outcome. It does not
print, read process arguments, or exit. Access it with:

- `outcome-kind`: `ok`, `help`, `version`, or `error`.
- `outcome-values`: parsed association list on success; version string for
  `version`; rendering context for `help` or `error`.
- `outcome-command`: the selected command declaration.
- `outcome-diagnostic`: on error, an association list with `argument`, `token`,
  and `message` fields.

`(ref values 'name [fallback])` retrieves an association-list value. `present?`
checks whether it contains a key, including keys inserted for defaults. Successful
values include `command-path`, a list of command-name strings. Optional options
without defaults receive `#f`; repeated options receive an empty list.

`format-help` takes a command and optional rendering context; `format-error`
takes an error outcome. Both return strings. `run` takes a command, callback, and
optional explicit argument list. It prints help/version to stdout and exits 0,
prints usage errors to stderr and exits 2, or calls the callback with the values
and exits with its returned status. There is no automatic `main` discovery.
