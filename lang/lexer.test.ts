import { expect, test } from "bun:test";
import { Lexer } from "./lexer";

test("basic lexer test", () => {
  const input = `42 "hello world" foo-bar ( ) + - * /`;
  const lexer = new Lexer(input);
  const tokens = lexer.tokenize();

  expect(tokens).toMatchInlineSnapshot(`
      [
        {
          "begin": 0,
          "column": 1,
          "end": 2,
          "line": 1,
          "sourceName": undefined,
          "type": "NUMBER",
          "value": "42",
        },
        {
          "begin": 3,
          "column": 4,
          "end": 16,
          "line": 1,
          "sourceName": undefined,
          "type": "STRING",
          "value": "hello world",
        },
        {
          "begin": 17,
          "column": 18,
          "end": 24,
          "line": 1,
          "sourceName": undefined,
          "type": "SYMBOL",
          "value": "foo-bar",
        },
        {
          "begin": 25,
          "column": 26,
          "end": 26,
          "line": 1,
          "sourceName": undefined,
          "type": "LPAREN",
          "value": "(",
        },
        {
          "begin": 27,
          "column": 28,
          "end": 28,
          "line": 1,
          "sourceName": undefined,
          "type": "RPAREN",
          "value": ")",
        },
        {
          "begin": 29,
          "column": 30,
          "end": 30,
          "line": 1,
          "sourceName": undefined,
          "type": "OP_ADD",
          "value": "+",
        },
        {
          "begin": 31,
          "column": 32,
          "end": 32,
          "line": 1,
          "sourceName": undefined,
          "type": "OP_SUB",
          "value": "-",
        },
        {
          "begin": 33,
          "column": 34,
          "end": 34,
          "line": 1,
          "sourceName": undefined,
          "type": "OP_MUL",
          "value": "*",
        },
        {
          "begin": 35,
          "column": 36,
          "end": 36,
          "line": 1,
          "sourceName": undefined,
          "type": "OP_DIV",
          "value": "/",
        },
        {
          "begin": 36,
          "column": 37,
          "end": 36,
          "line": 1,
          "sourceName": undefined,
          "type": "EOF",
          "value": "",
        },
      ]
    `);
});

test("comparison operators", () => {
  const input = `< <= > >= == !=`;
  const lexer = new Lexer(input);
  const tokens = lexer.tokenize();

  expect(tokens).toMatchInlineSnapshot(`
      [
        {
          "begin": 0,
          "column": 1,
          "end": 1,
          "line": 1,
          "sourceName": undefined,
          "type": "OP_LT",
          "value": "<",
        },
        {
          "begin": 2,
          "column": 3,
          "end": 4,
          "line": 1,
          "sourceName": undefined,
          "type": "OP_LTE",
          "value": "<=",
        },
        {
          "begin": 5,
          "column": 6,
          "end": 6,
          "line": 1,
          "sourceName": undefined,
          "type": "OP_GT",
          "value": ">",
        },
        {
          "begin": 7,
          "column": 8,
          "end": 9,
          "line": 1,
          "sourceName": undefined,
          "type": "OP_GTE",
          "value": ">=",
        },
        {
          "begin": 10,
          "column": 11,
          "end": 12,
          "line": 1,
          "sourceName": undefined,
          "type": "OP_EQ",
          "value": "==",
        },
        {
          "begin": 13,
          "column": 14,
          "end": 15,
          "line": 1,
          "sourceName": undefined,
          "type": "OP_NEQ",
          "value": "!=",
        },
        {
          "begin": 15,
          "column": 16,
          "end": 15,
          "line": 1,
          "sourceName": undefined,
          "type": "EOF",
          "value": "",
        },
      ]
    `);
});

test("field access", () => {
  const input = "hello.world";
  const lexer = new Lexer(input);
  const tokens = lexer.tokenize();

  expect(tokens).toMatchInlineSnapshot(`
    [
      {
        "begin": 0,
        "column": 1,
        "end": 5,
        "line": 1,
        "sourceName": undefined,
        "type": "SYMBOL",
        "value": "hello",
      },
      {
        "begin": 5,
        "column": 6,
        "end": 6,
        "line": 1,
        "sourceName": undefined,
        "type": "ACCESS",
        "value": ".",
      },
      {
        "begin": 6,
        "column": 7,
        "end": 11,
        "line": 1,
        "sourceName": undefined,
        "type": "SYMBOL",
        "value": "world",
      },
      {
        "begin": 11,
        "column": 12,
        "end": 11,
        "line": 1,
        "sourceName": undefined,
        "type": "EOF",
        "value": "",
      },
    ]
    `);
});

test("single line comments", () => {
  const input = `42 // this is a comment
  foo`;
  const lexer = new Lexer(input);
  const tokens = lexer.tokenize();

  expect(tokens).toMatchInlineSnapshot(`
    [
      {
        "begin": 0,
        "column": 1,
        "end": 2,
        "line": 1,
        "sourceName": undefined,
        "type": "NUMBER",
        "value": "42",
      },
      {
        "begin": 26,
        "column": 3,
        "end": 29,
        "line": 2,
        "sourceName": undefined,
        "type": "SYMBOL",
        "value": "foo",
      },
      {
        "begin": 29,
        "column": 6,
        "end": 29,
        "line": 2,
        "sourceName": undefined,
        "type": "EOF",
        "value": "",
      },
    ]
    `);
});

test("multi-line comments", () => {
  const input = `42 /* this is a
  multi-line comment */ foo`;
  const lexer = new Lexer(input);
  const tokens = lexer.tokenize();

  expect(tokens).toMatchInlineSnapshot(`
    [
      {
        "begin": 0,
        "column": 1,
        "end": 2,
        "line": 1,
        "sourceName": undefined,
        "type": "NUMBER",
        "value": "42",
      },
      {
        "begin": 40,
        "column": 25,
        "end": 43,
        "line": 2,
        "sourceName": undefined,
        "type": "SYMBOL",
        "value": "foo",
      },
      {
        "begin": 43,
        "column": 28,
        "end": 43,
        "line": 2,
        "sourceName": undefined,
        "type": "EOF",
        "value": "",
      },
    ]
    `);
});

test("nested multi-line comments", () => {
  const input = `42 /* outer /* inner */ still in outer */ foo`;
  const lexer = new Lexer(input);
  const tokens = lexer.tokenize();

  expect(tokens).toMatchInlineSnapshot(`
    [
      {
        "begin": 0,
        "column": 1,
        "end": 2,
        "line": 1,
        "sourceName": undefined,
        "type": "NUMBER",
        "value": "42",
      },
      {
        "begin": 42,
        "column": 43,
        "end": 45,
        "line": 1,
        "sourceName": undefined,
        "type": "SYMBOL",
        "value": "foo",
      },
      {
        "begin": 45,
        "column": 46,
        "end": 45,
        "line": 1,
        "sourceName": undefined,
        "type": "EOF",
        "value": "",
      },
    ]
    `);
});

test("comments mixed with division operator", () => {
  const input = `10 / 2 // division
  /* block comment */ 5 / 3`;
  const lexer = new Lexer(input);
  const tokens = lexer.tokenize();

  expect(tokens).toMatchInlineSnapshot(`
    [
      {
        "begin": 0,
        "column": 1,
        "end": 2,
        "line": 1,
        "sourceName": undefined,
        "type": "NUMBER",
        "value": "10",
      },
      {
        "begin": 3,
        "column": 4,
        "end": 4,
        "line": 1,
        "sourceName": undefined,
        "type": "OP_DIV",
        "value": "/",
      },
      {
        "begin": 5,
        "column": 6,
        "end": 6,
        "line": 1,
        "sourceName": undefined,
        "type": "NUMBER",
        "value": "2",
      },
      {
        "begin": 41,
        "column": 23,
        "end": 42,
        "line": 2,
        "sourceName": undefined,
        "type": "NUMBER",
        "value": "5",
      },
      {
        "begin": 43,
        "column": 25,
        "end": 44,
        "line": 2,
        "sourceName": undefined,
        "type": "OP_DIV",
        "value": "/",
      },
      {
        "begin": 45,
        "column": 27,
        "end": 46,
        "line": 2,
        "sourceName": undefined,
        "type": "NUMBER",
        "value": "3",
      },
      {
        "begin": 46,
        "column": 28,
        "end": 46,
        "line": 2,
        "sourceName": undefined,
        "type": "EOF",
        "value": "",
      },
    ]
    `);
});

test("only comments", () => {
  const input = `// just a comment
  /* and a block comment */`;
  const lexer = new Lexer(input);
  const tokens = lexer.tokenize();

  expect(tokens).toMatchInlineSnapshot(`
    [
      {
        "begin": 45,
        "column": 28,
        "end": 45,
        "line": 2,
        "sourceName": undefined,
        "type": "EOF",
        "value": "",
      },
    ]
    `);
});
