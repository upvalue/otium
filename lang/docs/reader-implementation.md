# Common reader implementation

The contract is in [reader-spec.md](../specs/reader-spec.md). C implements it in
`c/src/otium.c`; Chez uses ordinary Scheme libraries `reader.sls`, `values.sls`
and `truth.sls`. There is no bootstrap step. Both readers return ordinary
lists for array/table constructor sugar, including under quotation. Keywords
and nil are values, not syntax nodes.

Chez routes Otium files, required modules, REPL input and `read-string` through
`read-otium`. Implementation libraries, the launcher, native `.ss` tests and
explicit `chez/read` retain Chez's reader. The shared SDL scene loader now uses
`read-string`. Existing the former Chez examples sources already used parentheses;
no binding-bracket rewrite was needed there. Native hashtable adapters in the
condition tests and SDL host construct symbol keys explicitly rather than
reading `:type` as a symbol. Module grammar and suffix rules are unchanged.

The reader reports source names and port positions on Chez, and source names
and byte offsets on C. On unpositioned Chez ports, the offset counts
characters consumed by the current read. Chez has a separate incomplete-input condition for
unfinished strings, forms and abbreviations. Streaming input waits for more
characters; EOF within a datum reports incompleteness. `read-string` rejects
empty input and trailing datums. Shebangs are rejected at both file and string
boundaries, including executable C scripts. C read-string copies its source
bytes into a host-owned buffer because reading allocates and can move the
original managed string. Shared reader fixtures also run with GC stress to
cover this lifetime on both the semispace and generational collectors.

Chez arrays hold a backing vector and logical length. Growth preserves record
identity and doubles capacity when possible. Pop clears its removed slot.
Tables use native hashtables with an insertion-order list. New entries prepend
to that list in constant time; ordered enumeration reverses it. Deletion also
removes the key from the order list. Nil assignments delete entries. Strings
compare by contents, pairs structurally, and mutable collections by identity.
Exact and inexact number keys remain distinct. Pair keys freeze recursively
when hashed, including on lookup. Otium pair mutation rejects frozen keys and
cycles; explicitly native pair mutation bypasses those checks.

The collection surface includes construction, predicates, length, get, put!,
push!, pop!, keys, values, copy, reverse, map, filter, for-each, apply, take,
drop and update!. Chez `values` accepts one Otium table/nil as the collection
operation; other calls preserve native multiple values. Use `chez/values`
when returning exactly one nil across the native multiple-value boundary.
Native vectors and hashtables keep their own operations. Callable collections
and the remainder of C's prelude are still portability gaps.

Nil is Chez void. The expander quotes keyword and nil constants for Chez's
compiler. Hygienic private syntax emits false-or-void checks in `if`, `and`,
`or`, `cond`, `when`, `unless` and `while`; it does not call a public reloadable
truth function. `not`, filter, exists, for-all and condition-handler/catch
predicates use the same rule. Short-circuit expressions evaluate operands
once and retain their return values. Native Chez forms retain native truth
rules. The REPL and Otium display functions print nil and keywords using Otium
notation. Generic dispatch classifies nil and keywords separately, arrays as
`Vector`, and tables as `Hashtable`.

Numeric literal grammar is shared, but numeric representation and arithmetic
are not. Existing C range checks remain. Chez still has bignums and exact
arithmetic. The C token baseline rejects `...` as a malformed numeric token;
this also prevents using it as native syntax-rules ellipsis in Otium source.
Vertical bars and backslashes within atoms have no Chez quoting semantics.
Native syntax beyond the explicitly traversed Otium forms still has the
expansion limitations documented in [Chez notes](chez.md).

Both runtimes count Unicode code points for common string length/get. This
change does not unify native byte/character APIs or substring range behavior.

## Tests

The `reader` tests run 36 positive and 48 negative fixtures from
`tests/reader/data/reader-datums.ot`, covering datums, constructor lists,
numbers, strings, comments, delimiters and rejected host syntax, plus
evaluation, source files, required modules and streamed REPL input. The Chez reader tests
also check incomplete-input classification, positions, multiple values and the
native reader boundary.

The Chez reader also has a token mode for the formatter and linter, exposed as
`otium.reader/tokens`. It reads with the same code, so it accepts and rejects
the same text, and only records tokens when a token log is installed. Ordinary
reads check one flag per token. The C reader has no token mode. See
[development](development.md#formatting-and-linting).
