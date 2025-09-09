import { expect, test } from "bun:test";
import { Lexer } from "./lexer";
import { Parser } from "./parser";
import { EofValue, type OtExpr } from "./values";

function parseString(input: string): OtExpr[] {
  const lexer = new Lexer(input);
  const parser = new Parser(lexer);
  const expressions: OtExpr[] = [];

  let expr: OtExpr;
  while (true) {
    expr = parser.nextExpr();
    if (expr === EofValue) {
      break;
    }
    expressions.push(expr);
  }

  return expressions;
}

test("parse simple string", () => {
  const result = parseString('"hi"');
  expect(result).toMatchInlineSnapshot(`
    [
      "hi",
    ]
  `);
});

test("parse number", () => {
  const result = parseString("12345");
  expect(result).toMatchInlineSnapshot(`
    [
      12345,
    ]
  `);
});
