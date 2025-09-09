# lang

A simple Lisp-derived language with C-like syntax.

The current implementation is in TypeScript running under Bun (for prototyping -- this
will eventually gain a standalone compiler)

> bun lexer.ts scratch.ot
> bun parser.ts scratch.ot

to run the lexer and parser on an example file

and

> bun test lexer.test.ts
> bun test parser.test.ts

to run test suites
