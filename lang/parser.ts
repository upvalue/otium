import { EOF_TOKEN, Lexer, TOKEN_TYPES, type Token } from "./lexer";
import { Eof, EofValue, getSymbol, RParenVal, type OtExpr } from "./values";

export class ParserError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "ParserError";
  }
}

const BINDING_POWERS = {
  [TOKEN_TYPES.LPAREN]: 80,
  [TOKEN_TYPES.SYMBOL]: 0,
  [TOKEN_TYPES.STRING]: 0,
  [TOKEN_TYPES.NUMBER]: 0,
  [TOKEN_TYPES.RPAREN]: 0,
  [TOKEN_TYPES.EOF]: 0,
};

/**
 * This parser is a top down operator precedence parser; see the following
 * tutorials for some explanation as to how it works:
 *
 * Crockford's JS subset parser article: https://crockford.com/javascript/tdop/tdop.html
 * Eli Barzilay's article: https://eli.thegreenplace.net/2010/01/02/top-down-operator-precedence-parsing
 * (has some more call traces and explanatory text)
 *
 * Two differences so far: This one isn't defined in an OO style and it
 * returns something more akin to S-expressions than an abstract syntax
 * tree
 */

export class Parser {
  tokens: Token[];
  tokenIdx: number;
  currentToken: Token;
  trace: boolean = false;

  constructor(public lexer: Lexer) {
    this.tokens = [];
    this.tokenIdx = 0;
    this.currentToken = EOF_TOKEN;

    for (const token of lexer.tokenize()) {
      this.tokens.push(token);
    }
    this.currentToken = this.tokens[0];
  }

  peek(): Token {
    return this.tokens[this.tokenIdx]!;
  }

  advance(): Token {
    console.log("advance", {
      tokenIdx: this.tokenIdx,
      currentToken: this.currentToken,
    });
    this.currentToken = this.tokens[++this.tokenIdx]!;
    return this.currentToken;
  }

  nextExpr(rbp = 0): OtExpr {
    this.traceLog(`expr(rbp = ${rbp})`);
    let left = this.nud();
    this.traceLog(`expr(rbp = ${rbp}) => ${left}`);

    while (rbp < this.lbp()) {
      this.traceLog(`expr ${rbp} < ${this.lbp()} call led ${left}`);
      left = this.led(left);
    }

    return left;
  }

  lbp() {
    if (BINDING_POWERS[this.currentToken.type] === undefined) {
      throw new Error(
        `Don't know binding power for token type ${this.currentToken.type}`
      );
    }
    return BINDING_POWERS[this.currentToken.type];
  }

  led(left: OtExpr): OtExpr {
    this.traceLog("led");
    this.traceRet("led", () => {
      switch (this.currentToken.type) {
        // Left paren as a "binary operator" is how we implement
        // function calls
        case "LPAREN": {
          const vec = [left];
          console.log("left", { left });
          while (true) {
            const exp = this.nextExpr(80);
            console.log("exp", { exp });
            if (exp == RParenVal) {
              break;
            }
            vec.push(exp);
          }
          return vec;
        }
      }
    });
  }

  traceLog(msg: string) {
    console.log(
      `${this.currentToken.sourceName} ${this.currentToken.line}:${this.currentToken.column} ${this.currentToken.type} ${msg}`
    );
  }

  traceRet(msg: string, a: any): any {
    const r = a();

    this.traceLog(`${msg} => ${r}`);

    return r;
  }

  nud(): OtExpr {
    this.traceLog("nud");

    switch (this.currentToken.type) {
      case "NUMBER":
        this.advance();
        return parseInt(this.currentToken.value, 10);
      case "SYMBOL": {
        const ret = this.currentToken.value;
        this.advance();
        return ret;
      }
      case "STRING": {
        const ret = this.currentToken.value;
        this.advance();
        return ret;
      }
      case "RPAREN": {
        this.advance();
        return RParenVal;
      }
      case "LPAREN": {
        this.advance();
        console.log("currentToken", this.currentToken);
        const exp = this.nextExpr(0);
        // @ts-expect-error
        if (this.currentToken.type !== "RPAREN") {
          throw new Error("Expected closing paren");
        }
        this.advance();
        return exp;
      }

      case "EOF":
        return EofValue;
    }

    throw new Error(`unknown token type: ${this.currentToken.type}`);
  }
}

if (import.meta.main) {
  const args = process.argv.slice(2);
  if (args.length !== 1) {
    console.error("Usage: bun lexer.ts <file>");
    process.exit(1);
  }

  const filename = args[0];

  try {
    const fs = require("fs");
    const content = fs.readFileSync(filename, "utf8") as string;
    const lexer = new Lexer(content, filename);
    const parser = new Parser(lexer);
    parser.trace = true;

    let exp: OtExpr;
    while (true) {
      exp = parser.nextExpr();

      if (exp == EofValue) {
        break;
      }
    }
  } catch (error) {
    console.error(`Error reading file: ${error}`);
    process.exit(1);
  }
}
