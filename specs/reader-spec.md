# Common Otium reader

Status: agreed direction for the first implementation. Formatter and linter work
is deferred. This document specifies Otium source, not the Scheme used to
implement the Chez runtime.

## Scope and loading

Implement an Otium reader in ordinary Chez Scheme. Load it and its supporting
value definitions before reading Otium source. Route Otium source files,
required modules, REPL input, and the Otium string-reading API through it.
Keep Chez's native reader for implementation libraries (`.sls`), the launcher,
and explicitly native Scheme code. No cross-compilation or Otium bootstrap
compiler is required.

Update the C reader where needed to implement the same source conventions.
Share reader fixtures between runtimes. Share the contract and test cases;
the readers need not share implementation code.

Keep this implementation small. Do not introduce array/table syntax-node types,
a concrete syntax tree, a formatter, or a linter. Do not unify `let` semantics,
namespace loading conventions, or the numeric tower as part of this work.

## Lists, arrays, tables, and quotation

Read parenthesized lists and dotted pairs normally. Require matching closing
delimiters. Preserve the C reader's constructor sugar:

```scheme
[a b]       ; reads as (array a b)
{:a b}      ; reads as (table :a b)
'[a b]      ; reads as (quote (array a b))
'{:a b}     ; reads as (quote (table :a b))
```

The reader does not construct an array or table. Evaluation of the constructor
form does that. Quoted forms remain constructor lists. Macros receive ordinary
lists, with keyword values where present. Keep table entries in source order
until evaluation. Constructor argument validation belongs to the constructor.
Do not redesign existing dotted-tail handling in collection sugar incidentally.

Support quote, quasiquote, unquote, and unquote-splicing using `'`, backquote,
`,` and `,@`, with their existing list expansions, and read ``#` `` as
`(syntax-quote form)`. Omit Chez's syntax-object
abbreviations (`#'`, etc.), vector and bytevector literals, graph labels,
record literals, boxes, and gensym reader extensions from Otium source.

## Supporting values on Chez

Arrays are mutable and growable. Use a record holding a mutable backing vector
and logical length. Grow capacity geometrically, preserving the array record's
identity. Clear removed slots so unused capacity does not retain objects.
Provide the constructor and collection operations needed to match C's existing
array surface. Ordinary Chez vectors remain available through the native host
boundary; do not globally redefine them.

Tables may use Chez hashtables behind an Otium wrapper. Match C's existing key
equality, mutation, missing-key behavior, and relevant collection operations.
Inspect the current C implementation and tests rather than assuming Scheme's
`equal?` is equivalent. This is supporting work for usable collection syntax,
not a request for a new generic collection API.

Keywords are distinct from symbols and evaluate to themselves. `:color` reads
as an interned keyword named `color`; `'color` is a symbol and bare `color`
references a binding. Reject a bare `:`. Preserve the C treatment of `::name`
as a keyword whose name begins with `:`. Update Chez forms that currently
recognize option symbols such as `:as` to accept reader-produced keywords.
Do not use that adaptation to redesign `require` grammar.

Read `#t` and `#f` as booleans. Remove the bare `true` and `false` boolean
shortcuts from C: those spellings become ordinary symbols. Retain existing
lowercase `#true` and `#false` compatibility spellings unless a specific reason
requires revisiting them; the canonical documented spellings are `#t` and `#f`.

Read `nil` as a value distinct from `#f` and the empty list. On Chez, start with
the host void value as its representation. Native void results consequently
represent Otium nil at the host boundary. Print it as `nil` in Otium output and
provide the corresponding type/predicate behavior.

Both `#f` and nil are false in Otium; every other value, including the empty
list, zero, and empty collections, is true. Emit direct checks in expanded
code, or an equivalently inlinable private implementation. Avoid resolving a
reloadable public truthiness function on every branch. Preserve evaluation
count, short-circuiting, and operand return values in `and` and `or`.

Apply the rule consistently to Otium conditionals, `not`, loop tests, and
predicate-taking operations exposed as Otium operations. Native Chez code
continues using Chez truthiness. In particular, audit the direct exports of
`filter`, `exists`, and `for-all`; changing only `if` is insufficient.

## Numbers and identifiers

Support decimal integer literals, decimal floating-point literals including
exponents and leading fractional dots, and `0x`/`0X` hexadecimal integers with
optional sign. Examples: `12`, `-12`, `3.0`, `.5`, `-.5`, `1e3`, `0xff`, `-0x10`.

Use each runtime's existing numeric representations. Do not add common integer
range checks, arithmetic overflow checks, bignum emulation, or numeric-tower
restrictions to arithmetic. Large-number differences are explicitly deferred.
Do not remove existing runtime checks just to make the implementations match.

Reject unsupported literal forms, including rational and complex numbers and
Scheme radix/exactness prefixes, with reader errors. Restrict the accepted
notation before handing tokens to Chez's permissive numeric conversion.
Malformed number-like tokens must not silently become identifiers. Do not add
new infinity/NaN literal syntax in this implementation.

Preserve ordinary case-sensitive Otium symbols and qualified names such as
`space/name`. Use C's token conventions as the initial baseline; do not inherit
Chez's escaped identifiers, vertical-bar quoting, or lexical mode switches.
Do not broaden this task into redesigning identifier syntax. Record any awkward
existing edge cases, such as C's handling of `...`, for subsequent decisions.

## Strings

Support literal UTF-8 source text and these escapes on both runtimes:

| Escape | Meaning |
| --- | --- |
| `\n` | Line feed |
| `\r` | Carriage return |
| `\t` | Tab |
| `\"` | Double quote |
| `\\` | Backslash |
| `\0` | NUL |
| `\e` | Escape character |
| `\xHEX;` | Unicode scalar value, one or more hex digits, required semicolon |

Reject unknown escapes, malformed hex escapes, surrogate values, and scalar
values above U+10FFFF. Encode scalar escapes as UTF-8 in C and characters in
Chez. Normalize literal CRLF and lone CR inside source strings to LF. Explicit
`\r` still produces a carriage return. Support multiline strings; do not add
backslash-newline continuation or octal escapes.

String indexing and byte-versus-character APIs are outside this reader change.
Inspect current APIs before describing any remaining difference; they may have
changed independently of this specification.

## Comments, directives, and input handling

Support semicolon line comments and ordinary whitespace. Omit block comments,
datum comments, shebangs, and reader-state directives. Do not let Chez accept
these accidentally. Remove C's CLI-only shebang skipping so file and string
reading agree. Update the executable-script tests to reflect this deliberate
compatibility change.

Reject unsupported leading `#` forms explicitly. Preserve incremental reading
for the REPL: distinguish incomplete strings, forms, and quote prefixes from
invalid input. Report the source name and useful input position. Source-span
preservation for future formatting is not required now.

Expose an Otium `read-string` operation with C's existing single-datum contract:
allow surrounding whitespace/comments, reject empty input and trailing datums.
Source-file loading can read multiple forms. Keep native Chez `read` available
only as an explicitly native operation where appropriate.

## Migration and validation

Chez implementation files can keep bracketed Scheme binding lists. Otium
programs using those brackets as list delimiters need conversion to parentheses.
Identify the source-language boundary in tests and examples before changing
files. Keep README.md untouched.

Add shared positive and negative reader fixtures covering datum shape, quoted
constructor forms, keywords, booleans, nil, numbers, strings, comments, matching
delimiters, and unsupported host syntax. Verify evaluated collection literals,
mutation/growth, and nil behavior separately from reader output.

Test file loading, required modules, REPL input, and `read-string`. Include
short-circuit side effects and predicate callbacks returning nil. Exercise
macro arguments and expansion with the new keyword/nil values. Verify the Chez
runtime still starts from ordinary Scheme without bootstrapping.

Run focused C, Chez, and REPL checks, then the repository's applicable full
tests. Run `just gc-lint` if LLVM development tools are installed when touching
C allocation, roots, or managed pointers, as required by AGENTS.md. Record any
unrelated build blockers rather than silently fixing another task's work.

Update the language guide spec and relevant implementation notes to describe the resulting
behavior. Keep large-number and unrelated language differences explicit.
Benchmark the emitted nil checks on representative existing workloads before
claiming a performance result; microbenchmarks establish feasibility only.

## References

The C reader is in `c/src/reader.c`; collection reading is covered by
`c/tests/otium/01-reader.ot`. Chez source loading is in
`chez/otium/runtime.sls`, with expansion in `chez/otium/expander.sls`.

[SRFI 214](https://srfi.schemers.org/srfi-214/srfi-214.html) describes growable
vectors. Its API is prior art, not a dependency requirement.

The experimental
[racket-clojure](https://github.com/takikawa/racket-clojure) represents nil as
void and adds its own truth checks. It was verified running on Racket CS during
research. Checkouts are in `~/temp/lang-zoo/racket-clojure` and
`~/temp/lang-zoo/racket`. Direct Chez compilation of an inline false-or-void
test produced two comparisons with no helper call or allocation on ARM64.
This does not establish whole-program performance or justify copying that
language's implementation wholesale.
