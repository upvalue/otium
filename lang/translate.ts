// translate.ts -- translates Otium into JS code so it can be evaluated

// This is a mess! It's been put together just to get proof of concept going
// Needs to be rewritten.

import { Lexer } from "./lexer";
import { Parser } from "./parser";
import { beginSym, EofValue, type OtExpr, OtSymbol } from "./values";

const assertInvariant = (x: boolean, msg: string) => {
  if (!x) {
    throw new TranslateError(`Invariant violation: ${msg}`);
  }
};

class Env {
  vars: Record<string, string> = {};
  parent?: Env;
  depth: number = 0;

  constructor(parent?: Env) {
    if (parent) {
      this.parent = parent;
      this.depth = parent.depth + 1;
    }
  }

  define(name: string) {
    this.vars[name] = name;
  }

  isDefined(name: string): boolean {
    if (this.vars[name]) {
      return true;
    }

    if (this.parent) {
      return this.parent.isDefined(name);
    }

    return false;
  }

  lookup(name: string): string | undefined {
    if (this.vars[name]) {
      return this.vars[name];
    }

    if (this.parent) {
      return this.parent.lookup(name);
    }

    return undefined;
  }
}

export const rootEnv = new Env();

rootEnv.vars["print"] = "print";
rootEnv.vars["true"] = "true";
rootEnv.vars["false"] = "false";
rootEnv.vars["begin"] = "begin";
rootEnv.vars["<"] = "lt";
rootEnv.vars["-"] = "sub";
rootEnv.vars["+"] = "add";

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

    return `(function() { if(${translate(env, cond)} !== false) {
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

  ":=": (env: Env, args: OtExpr[]) => {
    assertInvariant(args.length == 2, "assignment is binary operator");

    const name = args[0]!,
      val = args[1]!;

    if (!(name instanceof OtSymbol)) {
      throw new TranslateError(
        `currently only symbols can be assigned to but got ${name}`
      );
    }

    env.define(name.name);
    const x = `const ${name.name} = ${translate(env, val)};`;
    return x;
  },

  fn: (env: Env, args: OtExpr[]) => {
    const fnEnv = new Env(env);

    const arglist: OtSymbol[] = [];
    let body: OtExpr = "";

    for (let i = 0; i != args.length; i++) {
      const arg = args[i];

      if (arg instanceof OtSymbol) {
        fnEnv.define(arg.name);
        arglist.push(arg);
      } else if (Array.isArray(arg)) {
        body = arg;
        break;
      } else {
        throw new TranslateError(
          `unexpected value in function definition argument list ${arg}`
        );
      }
    }

    if (body[0] !== beginSym) {
      throw new TranslateError(
        `expected function body to be enclosed in begin (braces)`
      );
    }

    return `(${arglist.map((a) => a.name)}) => {
return ${translate(fnEnv, body[1]!)};
}`;
  },
};

export const prelude = `
// prelude
const print = console.log;
const begin = (x) => x;
const lt = (a, b) => a < b;
const sub = (a, b) => a - b;
const add = (a, b) => a + b;

// begin main program
`;

export const translate = (env: Env, value: OtExpr): string => {
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
      .map((x) => translate(env, x))})`;
  } else if (value instanceof OtSymbol) {
    if (!env.isDefined(value.name)) {
      console.error(`WARNING: reference to undefined symbol ${value.name}`);
    }
    return env.lookup(value.name)!;
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
    console.log(prelude);
    while (true) {
      [, exp] = parser.nextExpr();

      if (exp == EofValue) {
        break;
      }

      console.log(translate(rootEnv, exp));
    }
  });
}
