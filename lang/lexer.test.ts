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
          "end": 2,
          "type": "NUMBER",
          "value": "42",
        },
        {
          "begin": 3,
          "end": 16,
          "type": "STRING",
          "value": "hello world",
        },
        {
          "begin": 17,
          "end": 24,
          "type": "SYMBOL",
          "value": "foo-bar",
        },
        {
          "begin": 25,
          "end": 26,
          "type": "LPAREN",
          "value": "(",
        },
        {
          "begin": 27,
          "end": 28,
          "type": "RPAREN",
          "value": ")",
        },
        {
          "begin": 29,
          "end": 30,
          "type": "ADD",
          "value": "+",
        },
        {
          "begin": 31,
          "end": 32,
          "type": "SUB",
          "value": "-",
        },
        {
          "begin": 33,
          "end": 34,
          "type": "MUL",
          "value": "*",
        },
        {
          "begin": 35,
          "end": 36,
          "type": "DIV",
          "value": "/",
        },
        {
          "begin": 37,
          "end": 38,
          "type": "PUNCTUATION",
          "value": ";",
        },
        {
          "begin": 38,
          "end": 38,
          "type": "EOF",
          "value": "",
        },
      ]
    `);
});