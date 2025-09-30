import { expect, test } from "bun:test";
import { Evaluator } from "./eval";

const evalString = (code: string) => {
  const evaluator = new Evaluator();
  return evaluator.evaluateString(code);
};

test("evaluator returns number 5", () => {
  const result = evalString("5");

  expect(result).toMatchInlineSnapshot(`5`);
});

test("evaluator returns string 'hello'", () => {
  const result = evalString('"hello"');
  expect(result).toMatchInlineSnapshot(`"hello"`);
});

test("evaluator returns boolean true", () => {
  const result = evalString("true");
  expect(result).toMatchInlineSnapshot(`true`);
});

test("evaluator can handle fibonacci", () => {
  const result = evalString(`
    fib := fn(n) {
      if(n < 2) {
        n
      } {
        fib(n - 1) + fib(n - 2)
      }
    }
    fib(10)
  `);
  expect(result).toMatchInlineSnapshot(`55`);
});

test("js syntax", () => {
  const result = evalString('js("12345")');
  expect(result).toMatchInlineSnapshot(`12345`);
});
