/**
 * translate.ts -- translates Otium into JS code so it can be evaluated
 *
 * How the translator works: The translator translates from Otium code
 * to JavaScript. Because the languages aren't semantically equivalent
 * (e.g. in Otium everything is an expression, only false is considered falsy)
 * there's a little bit of acrobatics required.
 *
 * Specifically, instead of just assuming that we can compile an OtExpr to some JS
 * equivalent, we give most expressions a "destination", a temporary variable.
 * This allows us to translate something like
 *
 * var := if(true) { true } { false }
 *
 * to
 *
 * let var;
 * let t1 = true, t2;
 * if(t1) {
 *   t2 = true;
 * } else {
 *   t2 = false;
 * }
 * var = t1;
 */

import { Lexer } from "./lexer";
import { Parser } from "./parser";
import { beginSym, EofValue, type OtExpr, OtSymbol } from "./values";
import { format } from "prettier";

/**
 * Destination describes where the
 */
type Destination = string | null;

class TranslateError extends Error {}

const assertInvariant = (x: boolean, msg: string) => {
  if (!x) {
    throw new TranslateError(`Invariant violation: ${msg}`);
  }
};

/**
 * Prelude or "standard library" currently just aliasing
 * a few things for convenience
 */
export const PRELUDE = `
// prelude
const print = console.log;
const lt = (a, b) => a < b;
const sub = (a, b) => a - b;
const add = (a, b) => a + b;

// begin main program
`;

/**
 * An environment; tracks variable definitions
 */
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

class Translator {
  temp = 0;
  rootEnv: Env;

  constructor() {
    const rootEnv = new Env();

    rootEnv.vars["print"] = "print";
    rootEnv.vars["true"] = "true";
    rootEnv.vars["false"] = "false";
    rootEnv.vars["begin"] = "begin";
    rootEnv.vars["<"] = "lt";
    rootEnv.vars["-"] = "sub";
    rootEnv.vars["+"] = "add";

    this.rootEnv = rootEnv;
  }

  makeDest(desc?: string | ((tempId: number) => string)) {
    let tempId = this.temp++;
    if (desc) {
      if (typeof desc === "string") {
        return `${desc}${tempId}`;
      } else {
        return desc(tempId);
      }
    }
    return `d${tempId}`;
  }

  /**
   * Syntax table. Right now things like if, fn etc are
   * implemented as special forms that are handled by
   * specific methods
   */
  syntaxTable = {
    // If expression syntax
    if: (env: Env, args: OtExpr[], destination: Destination): string => {
      const cond = args[0];
      const then = args[1];
      const elseb = args[2];

      if (!cond) {
        throw new TranslateError(
          `if syntax expected condition but none was found`
        );
      }

      if (!then) {
        throw new TranslateError(
          `if syntax expected then branch but none was found`
        );
      }

      const thenbody = then.slice(1);

      const conddest = this.makeDest("if_cond");

      let code = `let ${conddest};`;

      code += this.compile(env, cond, conddest);

      code += `\nif(${conddest} !== false) {\n`;

      code += this.compileVec(env, thenbody, destination);

      code += `\n}`;

      if (elseb) {
        const elsebody = elseb.slice(1);
        code += ` else {\n`;
        code += this.compileVec(env, elsebody, destination);
        code += `}\n`;
      } else {
        code += `\n`;
      }

      return code;
    },

    /** Assignment syntax */
    ":=": (env: Env, args: OtExpr, _destination: Destination): string => {
      assertInvariant(args.length === 2, "assignment is binary operator");
      const name = args[0]!,
        val = args[1]!;

      if (!(name instanceof OtSymbol)) {
        throw new TranslateError(
          `currently only symbols can be assigned to but got ${name}`
        );
      }

      env.define(name.name);
      return `const ${name.name} = ${this.compile(env, val, null)}`;
    },

    /** Function definition syntax */
    fn: (env: Env, args: OtExpr[], destination: Destination): string => {
      const fnEnv = new Env(env);

      const arglist: OtSymbol[] = [];
      let body: OtExpr[] = [];

      for (let i = 0; i !== args.length; i++) {
        const arg = args[i];

        if (arg instanceof OtSymbol) {
          fnEnv.define(arg.name);
          arglist.push(arg);
        } else if (Array.isArray(arg)) {
          if (i !== args.length - 1) {
            throw new TranslateError(
              `body must be last in function definition`
            );
          }
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

      let retDest = this.makeDest("return");
      let code = `(${arglist.map((a) => a.name)}) => {\nlet ${retDest};`;

      code += this.compileVec(fnEnv, body.slice(1), retDest);

      code += `\nreturn ${retDest};\n}\n`;

      return code;
    },
  };

  /**
   * main compiler call; given an environment, expression
   * and destination,
   */
  compile(env: Env, exp: OtExpr, destination: Destination): string {
    if (typeof exp === "number" || typeof exp === "string") {
      // Self evaluating expressions
      if (destination) {
        return `${destination} = ${JSON.stringify(exp)}`;
      } else {
        return `${JSON.stringify(exp)}`;
      }
    } else if (Array.isArray(exp)) {
      // Function call

      // Special case: check for syntax
      if (exp[0] instanceof OtSymbol) {
        const stx = this.syntaxTable[exp[0].name];
        if (stx) {
          return stx(env, exp.slice(1), destination);
        }
      }

      const applyOperator = this.makeDest("apply_operator");

      const applyOperands = exp
        .slice(1)
        .map((a, i) => this.makeDest((t) => `apply_operand${t}_${i}`));

      let code = `let ${applyOperator}, ${applyOperands};\n`;

      code += this.compile(env, exp[0]!, applyOperator);
      code += `\n`;

      for (let i = 1; i < exp.length; i += 1) {
        const opdest = applyOperands[i - 1];
        const operand = exp[i];
        code += this.compile(env, operand!, opdest!);
        code += `\n`;
      }

      // Can finally actually call the function
      const apply = `${applyOperator}(${applyOperands.join(", ")})`;

      code += ` \n${destination ? `${destination} = ${apply};` : `${apply};`}`;

      return code;
    } else if (exp instanceof OtSymbol) {
      // Variable
      const { name } = exp;
      if (!env.isDefined(name)) {
        console.error(`WARNING: Reference to undefined symbol ${name}`);
      }
      const nameResult = env.lookup(name)!;
      return destination ? `${destination} = ${nameResult};` : nameResult;
    }
  }

  /**
   * Compiles a list of expressions (ex a function body may have
   * multiple expressions, but only one of them actually returns
   * anything)
   */
  compileVec(env: Env, exp: OtExpr[], destination: Destination): string {
    let code = "";
    for (let i = 0; i !== exp.length; i += 1) {
      code += this.compile(
        env,
        exp[i]!,
        i == exp.length - 1 ? destination : null
      );
    }
    return code;
  }

  /** Compile a top level expression */
  compileToplevel(exp: OtExpr) {
    return this.compile(this.rootEnv, exp, null);
  }
}

import { runWithFile } from "./support";

if (import.meta.main) {
  runWithFile(async (content, filename) => {
    const lexer = new Lexer(content, filename);
    const parser = new Parser(lexer);
    const translator = new Translator();

    let exp: OtExpr;
    let accum = PRELUDE;
    while (true) {
      [, exp] = parser.nextExpr();

      if (exp == EofValue) {
        break;
      }

      accum += translator.compileToplevel(exp);
    }

    console.log(
      await format(accum, {
        parser: "babel",
      })
    );
  });
}
