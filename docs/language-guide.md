# ✨ Otium language guide

This guide is for programmers who already know some Scheme or Clojure. It covers how Otium differs from those languages and how to work with it day to day. For a shorter summary written for coding agents, see `AGENT-LANG.md`.

## What Otium is

Otium is a small, experimental Lisp. It aims to run in little memory, to be easy to change while it runs, and to still offer some features you'd expect from a bigger language. Its main ancestors:

- **Scheme**: calls, lexical closures, quotation, tail calls, `cons`/`car`/`cdr`, named `let`, conditions and restarts.
- **Clojure**: namespaces with `ns` and `require`, `ns/name` references, keywords, and `defonce` for state that should survive a reload.
- **Janet**: practical built-in collections. These are mutable arrays (`[...]`) and insertion-ordered tables (`{...}`), and you can call both as functions.

Otium also has a Lua-style `nil`. It is falsy and separate from the empty list, and putting it in a table deletes the key.

### Two runtimes

There are two implementations:

- **Chez Scheme** is the main one. It is the default, and the linter runs on it.
- **C** is experimental and can lag behind Chez. It is built with Meson, and the binary is `build/otium`.

The `./otium` launcher at the repository root picks the runtime:

```sh
./otium program.ot                  # Chez (default)
./otium --runtime=c program.ot      # C; run `just build` first
```

`chez/run` builds the native helpers it needs (line editing, the development server, the FFI helper) the first time they're used. A fresh checkout only needs Chez, Meson and a C compiler.

## Running programs and the REPL

The launcher takes options, then an optional script, then `--` and the program's own arguments:

```sh
./otium examples/greet.ot -- --count 3 Alice
./otium                         # no script: start the REPL
./otium --repl program.ot       # load a program, then start the REPL
./otium -L src program.ot       # add a module search directory
```

Run `./otium --help` for the full option list. The options are defined in `lib/catalog/options.json`, and `tools/generate-cli.py` generates the help text and the runtimes' option tables from that file. The common options are `--help`, `--repl`, `--path`/`-L`, `--project`, `--no-project`, `--load`, `--dev` and `--dev-watchdog`. Options starting with `c.` (for example `--c.heap-max=BYTES`) only apply to the C runtime.

There are no `run` or `repl` subcommands. You run a file by naming it, and you get a REPL by passing no script or by passing `--repl`. The launcher does know these subcommands:

| Command | What it does |
| --- | --- |
| `./otium fmt [--check] PATH...` | Format Otium source in place. `--check` lists files that would change and exits 1. |
| `./otium lint PATH...` | Run the linter (see [Lint](#lint)). |
| `./otium explain CODE` | Explain an error code or lint rule, such as `E0201` or `L0001`. |
| `./otium serve` | Run a development server with no program (see [Live reloading](#live-reloading-and-the-development-server)). |
| `./otium attach` | Connect to a running development server. |
| `./otium mcp` | Expose a running development server to an LLM client as MCP tools over stdio. |

There is no `otium test` command either. Tests run through Meson with `just test`, and you can pass suite names (`c`, `chez`, `shared`, `native`, `gpu`, `bench`) or test names. Test files usually use the small `otium.test` library in `lib/otium/test.ot`:

```scheme
(require otium.test :as t)

(t/check "addition" 4 (+ 2 2))
(t/check #t (t/fails? (fn () (car nil))))
(t/report "arithmetic")
```

`check` exits with status 1 on the first mismatch. The `Justfile` also has `just fmt` and `just lint`, which run on `lib`, `examples`, `benchmarks` and `manse` when you give them no arguments.

In the REPL, `(describe 'name)` shows a binding's documentation. Use it before assuming a Scheme or Clojure function exists in Otium or works the same way.

## Syntax and data

Source files use the `.ot` extension. Names are kebab-case, predicates end in `?`, and functions that mutate end in `!`. Comments start with `;`.

### Literals

| Syntax | Value |
| --- | --- |
| `42`, `-7`, `0xff` | integers |
| `3.14`, `1e-3` | floats |
| `#t`, `#f` | booleans |
| `nil` | the absent value |
| `"text\n"` | string |
| `#\a`, `#\space` | character |
| `#u8(1 2 3)` | bytevector |
| `'name` | symbol |
| `:name` | keyword (evaluates to itself) |
| `'(1 2 3)` | literal list |
| `[a b c]` | mutable array; elements are evaluated |
| `{:k v ...}` | mutable, insertion-ordered table; keys and values are evaluated |

Things that may surprise you:

- **There are no rational literals.** `1/2` is a read error (E0004). Division keeps exact integer results when it can and otherwise gives a float: `(/ 6 3)` is `2` and `(/ 3 2)` is `1.5`. Chez has bignums, but integers in the C runtime are bounded.
- **Square brackets always build an array.** They never replace parentheses. `(let [x 1] ...)` and `(fn [x] ...)` are errors.
- **Other `#` syntax is reserved.** That includes `#(...)`, `#{...}` and `#_`.
- **String escapes** are `\n`, `\r`, `\t`, `\0`, `\e`, `\"`, `\\` and `\xHEX;`.
- Indexing a string with `get` returns a character, not a one-character string.

### Truth and `nil`

Only `nil` and `#f` are falsy. Zero, `""` and the empty list `'()` are all truthy. `nil` is not the empty list: use `nil?` to test for a missing value and `null?` to test for an empty list. An `if` without an else branch returns `nil`.

### Tables and arrays

```scheme
(def config {:size 12 :theme "dark"})

(config :size)            ; 12, tables are callable
(:theme config)           ; "dark", so are keywords
(get config :missing 0)   ; 0, get takes an optional default
(put! config :size 14)    ; mutate in place
(put config :size 16)     ; a changed shallow copy; config is untouched
(put! config :theme nil)  ; putting nil deletes the key

(def items [10 20 30])
(items 0)                 ; 10, arrays are zero-based
(push! items 40)          ; append
(pop! items)              ; 40
```

`get` returns `nil` for anything missing, and `get` on a `nil` collection also returns `nil`. When a table needs to hold a false value, store `#f`. `put!` on an array only replaces indexes that already exist; use `push!` to make the array longer. `copy` is shallow. `assoc` is the Scheme association-list lookup, not Clojure's `assoc`.

Equality: `equal?` compares structure, `eq?` compares identity, and `=` compares numbers.

### Sequences

`map`, `filter`, `take`, `drop` and `sort` return the same kind they were given: a list gives a list and an array gives an array. `map` takes a single sequence. `reduce` takes the initial value before the sequence. `range` returns an array and leaves out its end value.

```scheme
(map (fn (x) (* x x)) [1 2 3])     ; [1 4 9]
(filter odd? '(1 2 3 4))           ; (1 3)
(reduce + 0 (range 5))             ; 10
```

Other everyday functions include `str` (concatenate display forms), `println`, `string-split`, `string-join`, `keys`, `values` (a table's values, not Scheme multiple values), `length` and `count` (which treats `nil` as 0).

## Definitions and functions

```scheme
(def limit "Maximum entries." 100)

(def (area w h)
  "Area of a w by h rectangle."
  (* w h))

(def square (fn (x) (* x x)))

(def (log-all prefix & messages)
  (for-each (fn (m) (println prefix m)) messages))
```

- `def` defines both values and functions. `define`, `defn`, `lambda`, `let*` and `letrec` do not exist, and the linter flags them (L0101).
- A docstring goes after the parameter list, or before the value in a value definition.
- Optional positional parameters follow `&optional`, each as `(name default)`. Defaults run only when omitted, from left to right, and can refer to preceding parameters. An explicit `nil` or `#f` is a supplied value.
- A rest parameter can be written `(x . rest)`, `(x & rest)` or as a single symbol. The rest arrives as a list, allocating when extra arguments are supplied. `rest-arguments` (L0401) warns on runtime rest parameters; suppress it with a reason for genuinely variadic APIs. Macro parameter lists are exempt.
- Optional parameters are supported in `fn`, `def` and `def-`. Required parameters come first; optional and rest parameters cannot be combined. Keyword parameters are not supported.
- `def-` makes a definition private to its namespace.
- `def` is allowed at top level and at the start of a body. `def-`, `defmacro`, `ns` and `require` are only allowed at top level.

```scheme
(def (draw-text text x y &optional (color :white) (scale 1))
  ...)

(draw-text "hello" 10 20)          ; :white, 1
(draw-text "hello" 10 20 :red)     ; :red, 1
(draw-text "hello" 10 20 :red 2)   ; :red, 2

(fn (items start &optional (end (length items)))
  ...)
```

Calls remain positional: supply earlier arguments to reach later ones. Too
many or too few arguments raise an arity error. Chez compiles optional
arguments to fixed-arity `case-lambda` entries with one shared body and no
rest-list allocation. The experimental C runtime implements the same
semantics using a rest-list fallback.

### `let` is sequential

Plain `let` behaves like Scheme's `let*`, so a later binding can use an earlier one:

```scheme
(let ((x 10)
      (y (+ x 2)))
  y)                                ; 12
```

Named `let` is the usual way to write a tail-recursive loop. On Chez its initial values are computed in the outer scope, so compute any dependent starting values in a surrounding plain `let`.

```scheme
(let loop ((i 0) (total 0))
  (if (= i 5)
      total
      (loop (+ i 1) (+ total i))))
```

## Control flow

`if`, `cond`, `case`, `when`, `unless`, `and`, `or` and `begin` work as they do in Scheme. `and` and `or` return the value that decided the result. `set!` changes a variable. There are also two stateful loops:

```scheme
(while (< n 10) (set! n (+ n 1)))
(dotimes (i 3) (println i))         ; 0 1 2
```

`set!` can also assign through a callable place, such as `(set! (button :presses) 0)`.

### Errors and cleanup

Errors are conditions, and conditions are tables.

```scheme
(try
  (car nil)
  (catch (error? problem)
    (println "failed:" (condition-message problem))))

(defer (use-file f)
  (close-file f))                   ; cleanup runs on return or unwinding
```

`(catch name ...)` with no predicate catches any condition. `handler-bind` and `restart-case` let you recover at the place where the condition was raised, in the Common Lisp style.

For dynamically scoped settings, use parameters:

```scheme
(defparam indent 2)
(with-params ((indent 4))
  (indent))                         ; 4
```

## `match`

`match` takes a value apart by its shape. It is a macro in `lib/otium/core.ot` and behaves the same on both runtimes.

```scheme
(match event
  ({:type :click :pos [x y]} (click! x y))
  ({:type :key :key k} :when (printable? k) (insert! k))
  (`(move ,dx ,dy) (move! dx dy))
  (_ nil))
```

- A bare name always binds. `_` matches anything and binds nothing. Use `(== expr)` to compare against an existing variable.
- Literals and `'datum` patterns compare with `equal?`, so `1` does not match `1.0`.
- A table pattern needs each listed key to be present and ignores other keys. Use `(opt p)` or `(opt name default)` for optional keys.
- An array pattern matches that exact length. `[a b & _]` matches arrays of at least that length.
- `(list a b & rest)`, `(Class :slot p)`, `(? pred p)`, `(app f p)`, `(and ...)` and `(or ...)` are also supported.
- If no clause matches, `match` raises a `match-error`.

The full pattern table is in `specs/match.md`.

## Macros

`defmacro` receives the unevaluated forms and returns code. Write the code it returns with the `` #` `` template, plus `,` and `,@`:

```scheme
(defmacro twice (x)
  #`(let ((v# ,x)) (+ v# v#)))
```

Inside a `` #` `` template:

- Symbols are qualified with the namespace the macro was written in. The expansion therefore works from any namespace, can reach `def-` helpers, and can't be captured by the caller's local variables.
- A name that the expansion binds must end in `#`, as in `v#`. This gives a fresh symbol each time. A bare binding name is an error (E0113).
- Quoted symbols stay unqualified. Write `,'name` when you want the caller's `name` on purpose.

Plain backquote is still available for building data. After you change a macro, the code that uses it has to be expanded again, which usually means reloading the definitions that call it.

## Classes and generic functions

Otium has classes with fixed slots and generic functions that dispatch on the classes of their arguments:

```scheme
(defclass Button
  label
  (presses :init 0))

(defgeneric (handle-event! widget event))

(defmethod (handle-event! (widget Button) event)
  (set! (widget :presses) (+ 1 (widget :presses))))
```

You create instances with `make`. Their slots are read by calling the instance, like a table. Methods can be `:around` methods and can bind the next method with `:next super`, then call `(super arguments ...)`. A reload keeps class identity and existing methods. See `examples/multimethods/events.ot` and `specs/classes.md`.

## Namespaces and modules

A module starts with `ns`. Dots in a namespace name map to directories, so `app.tools` lives in `app/tools.ot` under one of the load paths. A file's `ns` must match the name it was loaded under (E0206).

```scheme
(ns app.main)
(require app.tools :as tools)
(require otium.extra :refer (->))

(tools/work (-> 1 inc))
```

- `:as` sets an alias, and `:refer (name ...)` imports chosen names. Aliases keep dependencies easy to see.
- Several modules can be required in one form: `(require otium.process (otium.format :as format))`.
- Requires can also go in the `ns` form: `(ns app.main (:require (app.tools :as tools)))`.
- An alias can't be spelled like an existing namespace name (E0208). A definition can't share a name with an import (E0207).
- `(require chez)` gives access to native Chez Scheme names as `chez/...`. Treat this as an explicit way out of Otium and keep ordinary code on the shared Otium API.

### Load paths and `project.ot`

When looking for modules, the launcher searches these places in order:

1. Directories given with `-L`/`--path`.
2. Directories in the `OTIUM_PATH` environment variable, separated by colons.
3. The `(paths ...)` entry of a `project.ot` file. The launcher looks for one in the current directory and then in each parent directory. Use `--project FILE` to name a different file, or `--no-project` to skip it.

If none of these give a path, the current directory is used. Paths in `project.ot` are relative to that file. The repository's own `project.ot` is:

```scheme
; Runnable examples for either runtime.
(paths "examples" "examples/otcl")
```

`project.ot` can also set lint levels with `(lint :allow (...) :warn (...) :deny (...))`.

## Errors and diagnostics

Every error and lint rule has a code. The catalog is `lib/catalog/diagnostics.json`, and `tools/generate-diagnostics.py` generates `lib/otium/diagnostics/catalog.ot` from it. Each entry has a title, an explanation, and a broken and fixed example.

| Range | Kind |
| --- | --- |
| `E00xx` | reader errors, such as an unclosed form (E0001) or an invalid number (E0004) |
| `E01xx` | syntax errors, such as a bad binding list (E0102) or a definition in the wrong place (E0105) |
| `E02xx` | name and namespace errors, such as an unbound name (E0201), a namespace not found (E0202) or a circular load (E0205) |
| `E03xx` | call errors: wrong number of arguments (E0301), value not callable (E0302) |
| `E04xx` | type errors (E0401) |
| `L0xxx` | lint rules |

```sh
./otium explain E0201
./otium explain L0001
```

Many of these errors exist to catch Scheme or Clojure habits. For example, E0201 suggests `def` when you wrote `define`, and E0102 explains that `[x 1]` is not a binding list.

### Lint

```sh
./otium lint path/to/file.ot
./otium lint --rules                 # list rules and their default levels
./otium lint --format=json src       # also: human (default), short
```

Rules have a level of `allow`, `warn` or `deny`. A deny-level finding makes the command fail, and `--deny-warnings` makes warnings fail it too. `--allow RULE`, `--warn RULE` and `--deny RULE` on the command line override the project's settings. The current rules are:

- **Style:** `if-chain` (an `if` in the else branch of an `if`; use `cond`), `liftable-function`.
- **Performance:** `rest-arguments` (L0401, warn by default).
- **Foreign forms:** `foreign-definition` (`define`, `defn`, `lambda` and so on), `bracket-bindings` (deny by default).
- **Correctness:** `unbound-name` (deny), `unused-binding`.
- **Macros:** `missed-unquote`, `captured-binding`, `template-namespace-ref`, `self-qualified-template`, `expansion-side-effect`, `expansion-failed`.
- **Suppressions:** `unused-suppression`, `unexplained-suppression` (deny), `unknown-rule` (deny).

The name checks load your dependencies and run your macros. Pass `--no-expand` to check syntax only.

To suppress a rule for the next form, add a comment before it or at the end of the form's first line. To suppress it for a whole file, put an `allow-file` comment before the first form. Always give a reason: a suppression without one, or one naming an unknown rule, is an error.

```scheme
;; lint: allow(if-chain) mirrors the three cases in the protocol spec
(def (kind x) (if (a? x) :a (if (b? x) :b :c)))

;; lint: allow-file(liftable-function) helpers read better next to their callers
```

## Live reloading and the development server

Live reloading is central to Otium. The rules:

- **References between namespaces are live.** When you redefine a function, every caller that refers to it by name uses the new code.
- **`defonce` survives a reload.** Use it for state, and plain `def` for code you want replaced.
- **A saved function value keeps its old code.** If a callback you've stored somewhere should pick up redefinitions, wrap it: `(fn args (apply tools/handler args))`.
- **`(require app.tools :as tools :reload)` evaluates the module again.** Definitions you deleted from the file stay defined. A reload is also not atomic: forms that ran before an error stay in effect.

`examples/reload.ot` and `examples/demo/counter.ot` show this. The counter keeps its state in a `defonce`, and the callback keeps working after the reload:

```scheme
(ns demo.counter)

(defonce state [0])

(def (tick!)
  (put! state 0 (+ (get state 0) 1))
  (get state 0))
```

### The development server

A program started with `--dev` listens on a local Unix socket. By default the socket is `/tmp/otium-UID/PID.sock`, in a directory only you can read; use `--dev=PATH` to choose another path. Tools can then evaluate code inside the running program, reload files, and pause code that has hung. Both runtimes support this.

```sh
./otium --dev examples/ui.ot        # a program with a dev socket
./otium serve                       # a server with no program
./otium serve --load setup.ot       # preload a file, then serve
```

Requests are handled between UI frames, at the REPL prompt, or by `otium serve`. If an evaluation runs longer than the watchdog limit (2000 ms by default; change it with `--dev-watchdog=MS`, and 0 turns it off), it is paused instead of killed. While it's paused you can still inspect the program, then continue or abort it. An error inside a UI frame does not close the window.

There are three clients:

- `tools/otium-dev` runs one request per call, which suits scripts and agents. Its subcommands are `ls`, `status`, `eval`, `load`, `events`, `capture` (save the next UI frame as an image), `interrupt`, `continue` and `abort`.

  ```sh
  tools/otium-dev eval '(count (ui/windows))' --ns ui.layout
  tools/otium-dev load examples/ui/layout.ot
  ```

- `./otium attach` opens an interactive session.
- `./otium mcp` offers the same operations as MCP tools: `eval`, `load`, `status`, `interrupt`, `continue` and `abort`.

The server cannot interrupt a blocking foreign call, and evaluation in a paused program only sees namespace bindings, not the paused function's local variables. The protocol is described in `specs/dev-server.md`.

## The FFI

`otium.ffi` (`lib/otium/ffi.ot`) loads C shared libraries and calls their functions directly, with no C wrapper code. It works on both runtimes. The main pieces are:

- `open-library` and `bind` to load a library and bind a function.
- `callback` to pass an Otium function to C.
- `alloc`, `ref`, `set!`, `sizeof` and the struct and union helpers for native memory.
- The `defbindings` macro, which declares a group of bindings along with a loader function.

Each binding states the C symbol, its argument types, its result type, and whether the call is safe to run during garbage collection. FFI failures are conditions; test for them with `ffi/error?`.

To see real bindings, look at:

- `lib/otium/sqlite/raw.ot` for SQLite, wrapped by `lib/otium/sqlite.ot`.
- `lib/otium/sdl.ot` and `native/sdl/bindings.ot` for SDL.
- `lib/otium/objc.ot` for Objective-C messaging on macOS.
- `manse/ghostty.ot` for libghostty.

`specs/ffi.md` is the full design. Parts of it are still being built.

## A tour of the standard library

Everything under `lib/otium/` is ordinary Otium:

| Namespace | File | What it has |
| --- | --- | --- |
| `otium.core` | `lib/otium/core.ot` | The shared prelude, loaded automatically: `when`, `case`, `dotimes`, sequence functions, sorting, `put`, condition helpers and `match`. |
| `otium.extra` | `lib/otium/extra.ot` | Optional helpers: `->`, `->>`, `partial`, `comp`, `group-by`, `frequencies`. You have to require it. |
| `otium.test` | `lib/otium/test.ot` | `check`, `fails?`, `report`. |
| `otium.cli` | `lib/otium/cli.ot` | Command-line parsing from a declared spec. See `examples/greet.ot`. |
| `otium.json` | `lib/otium/json.ot` | `parse` and `encode`. `json/null` stands for JSON `null`. |
| `otium.time` | `lib/otium/time.ot` | `seconds`. |
| `otium.resource` | `lib/otium/resource.ot` | Owned resources and the `close` generic. |
| `otium.ffi` | `lib/otium/ffi.ot` | The FFI. |
| `otium.objc` | `lib/otium/objc.ot` | Objective-C messaging. |
| `otium.sqlite` | `lib/otium/sqlite.ot` | SQLite connections, statements, transactions and a query builder (`select`, `insert-into`, `compose`). |
| `otium.sdl`, `otium.gfx` | `lib/otium/sdl.ot`, `lib/otium/gfx.ot` | SDL bindings and drawing. |
| `otium.ui` | `lib/otium/ui.ot`, `lib/otium/ui/` | A widget library: labels, buttons, text fields, scrolling, focus, windows. See `specs/ui.md` and `examples/ui.ot`. |
| `otium.dev` | `lib/otium/dev.ot` | The development server's request handling. |
| `otium.syntax`, `otium.format`, `otium.lint`, `otium.diagnostics` | | Source trees, the formatter, the linter and diagnostic rendering. `lib/otium/tools/` has the `fmt`, `lint` and `explain` command entry points. |

Each runtime documents its built-in functions with `describe`. `lib/catalog/runtime.json` lists each one along with the runtimes that provide it.

## Where to go next

- [Otium docs](README.md) is the index of these docs.
- The [Manse guide](manse/guide.md) covers Manse, a development environment written in Otium and running on Ghostty.
- The [otos guide](otos/guide.md) covers otos.
- `specs/` has the designs: `specs/language.md`, `specs/match.md`, `specs/classes.md`, `specs/io.md`, `specs/ffi.md`, `specs/dev-server.md` and `specs/ui.md`. Some of them describe work that isn't finished yet; check the status line at the top of each one.
- `examples/` has runnable programs, from `examples/greet.ot` and `examples/reload.ot` to `examples/ui.ot` and the solved puzzles in `examples/hand/`.
- `AGENT-LANG.md` is the condensed language summary, and `lib/otium/core.ot` is the prelude itself.
