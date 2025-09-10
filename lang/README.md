# lang

A simple Lisp-derived language with C-like syntax.

The current implementation is in TypeScript running under Bun (for prototyping -- this
will eventually gain a standalone runtime and compiler). It's very much been thrown together
and is still changing a lot.

> bun lexer.ts scratch.ot

> bun parser.ts scratch.ot

> bun translate.ts scratch.ot

to run the lexer, parser or translator on an example file

# How to run Otium code

The translator produces some (pretty cursed) JavaScript code as its output, which can then
be run with a JS interpreter

> bun translate.ts scratch.ot > scratch.js

> bun scratch.js

# Testing

> bun test lexer.test.ts

> bun test parser.test.ts

to run test suites
