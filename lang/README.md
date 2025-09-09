# lang

a simple lisp-derived language with c-like syntax

bun is required to run

currently there is a lexer and a partially complete parser; each component can be invoked
individually to see the output

> bun lexer.ts scratch.ot
> bun parser.ts scratch.ot

to run on an example file 

and

> bun test lexer.test.ts
> bun test parser.test.ts

to run test suites
