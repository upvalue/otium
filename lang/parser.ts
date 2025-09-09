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
 * resources for some explanation as to how it works:
 *
 * Crockford's JS subset parser article: https://crockford.com/javascript/tdop/tdop.html
 * Eli Barzilay's article: https://eli.thegreenplace.net/2010/01/02/top-down-operator-precedence-parsing
 * (has some more call traces and explanatory text)
 * Lua parser source: https://raw.githubusercontent.com/lua/lua/refs/heads/master/lparser.c
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
    this.currentToken = this.tokens[++this.tokenIdx]!;
    return this.currentToken;
  }

  primaryExpr(): OtExpr {}

  suffixedExpr(): OtExpr {
    this.primaryExpr();
  }

  simpleExpr(): OtExpr {
    const token = this.currentToken;

    if (token.type === "EOF") {
      return EofValue;
    } else if (token.type === "STRING") {
      return token.value;
    } else if (token.type === "NUMBER") {
      return parseInt(token.value, 10);
    } else {
      return this.suffixedExpr();
    }
  }

  nextExpr(limit = 0): OtExpr {
    const exp = this.simpleExpr();
    this.advance();
    return exp;
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

      console.log("exp", { exp });
    }
  } catch (error) {
    console.error(`Error reading file: ${error}`);
    process.exit(1);
  }
}
