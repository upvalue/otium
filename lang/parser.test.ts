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
    [, expr] = parser.nextExpr();
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

test("basic function call", () => {
  const result = parseString('print("hello")');
  expect(result).toMatchInlineSnapshot(`
    [
      [
        OtSymbol {
          "name": "print",
        },
        "hello",
      ],
    ]
  `);
});

test("basic arithmetic expression", () => {
  const result = parseString("5 * 5 + 10");
  expect(result).toMatchInlineSnapshot(`
    [
      [
        OtSymbol {
          "name": "+",
        },
        [
          OtSymbol {
            "name": "*",
          },
          5,
          5,
        ],
        10,
      ],
    ]
  `);
});

test("brace splicing", () => {
  const result = parseString("if(true) { true }");
  expect(result).toMatchInlineSnapshot(`
    [
      [
        OtSymbol {
          "name": "if",
        },
        OtSymbol {
          "name": "true",
        },
        [
          OtSymbol {
            "name": "begin",
          },
          OtSymbol {
            "name": "true",
          },
        ],
      ],
    ]
  `);
});

test("define within define", () => {
  expect(() => parseString("x := x := 5")).toThrowErrorMatchingInlineSnapshot(
    `"cannot nest definitions/assignments"`
  );
});

test("function with no args", () => {
  const result = parseString("funcall()");
  expect(result).toMatchInlineSnapshot(`
    [
      [
        OtSymbol {
          "name": "funcall",
        },
      ],
    ]
  `);
});
