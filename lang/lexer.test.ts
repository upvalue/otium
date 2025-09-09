import { expect, test } from "bun:test";
import { Lexer } from "./lexer";

test("basic lexer test", () => {
    const input = `42 "hello world" foo-bar ( ) + - * / ;`;
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
          "begin": 37,
          "column": 38,
          "end": 38,
          "line": 1,
          "sourceName": undefined,
          "type": "PUNCTUATION",
          "value": ";",
        },
        {
          "begin": 38,
          "column": 39,
          "end": 38,
          "line": 1,
          "sourceName": undefined,
          "type": "EOF",
          "value": "",
        },
      ]
    `);
});