// translate.ts -- translates Otium into JS code so it can be evaluated
import { Lexer } from "./lexer";
import { Parser } from "./parser";
import { EofValue, OtExpr, OtSymbol } from "./values";

class Env {
  vars: Record<string, string> = {};
  parent?: Env;
  depth: number = 0;
}

const rootEnv = new Env();

class TranslateError extends Error {}

/**
 * Syntax table -- these magical forms invoke the translator
 */
const syntaxTable = {
  if: (env: Env, args: OtExpr[]) => {
    const cond = args[0];
    const then = args[1];
    const elseb = args[2];

    if (!then) {
      throw new TranslateError(
        `if syntax expected then branch but none was found`
      );
    }

    return `(function() { if(${translate(env, cond)}) {
\treturn ${translate(env, then)}
  }${
    elseb
      ? ` else {
    return ${translate(env, elseb)}
}`
      : ""
  }
})();`;
  },
};

const prelude = `
// prelude
const print = console.log;

// begin main program
`;

const translate = (env: Env, value: OtExpr): string => {
  if (typeof value === "number" || typeof value === "string") {
    return JSON.stringify(value);
  } else if (Array.isArray(value)) {
    if (value[0] instanceof OtSymbol) {
      const stx = syntaxTable[value[0].name];
      if (stx) {
        return stx(env, value.slice(1));
      }
    }
    // Function call or syntax
    return `${translate(env, value[0]!)}(${value
      .slice(1)
      .map((x) => translate(env, x))});`;
  } else if (value instanceof OtSymbol) {
    return value.name;
  } else {
    throw new TranslateError(`don't know how to handle ${value}`);
  }
};

import { runWithFile } from "./support";

if (import.meta.main) {
  runWithFile((content, filename) => {
    const lexer = new Lexer(content, filename);
    const parser = new Parser(lexer);

    let exp: OtExpr;
    while (true) {
      [, exp] = parser.nextExpr();

      if (exp == EofValue) {
        break;
      }

      console.log(prelude);

      console.log(translate(rootEnv, exp));
    }
  });
}
