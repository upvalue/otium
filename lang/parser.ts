import { EOF_TOKEN, Lexer, type Token, type TokenType } from "./lexer";
import { beginSym, EofValue, getSymbol, type OtExpr } from "./values";

export class ParserError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "ParserError";
  }
}

/**
 * Priority for operators -- higher is stronger.
 * There's no right-associative operators yet,
 * if we add those there'll need to be two values
 */
const PRIORITY: Partial<Record<TokenType, number>> = {
  // Arithmetic
  // +
  OP_ADD: 3,
  // -
  OP_SUB: 3,
  // *
  OP_MUL: 3,
  // /
  OP_DIV: 3,

  // Comparison operators
  // <
  OP_LT: 2,
  // >
  OP_GT: 2,
  // <=
  OP_LTE: 2,
  // >=
  OP_GTE: 2,

  // ==
  OP_EQ: 2,
  // !=
  OP_NEQ: 2,

  // Assignment :=
  OP_ASSIGN: 10,
};

const isOperator = (token: Token) => token.type.startsWith("OP_");

/**
 * Special return value stating that the parser has encountered
 * a stronger operator
 */
class StrongerOperator {
  constructor(public operator: Token) {}
}

const splat = (exp: OtExpr): OtExpr[] => {
  if (Array.isArray(exp)) {
    return exp;
  }
  return [exp];
};

/**
 * This parser is a top down operator precedence parser; see the following
 * resources for some explanation as to how it works:
 *
 * Crockford's JS subset parser article: https://crockford.com/javascript/tdop/tdop.html
 * Eli Barzilay's article: https://eli.thegreenplace.net/2010/01/02/top-down-operator-precedence-parsing
 * (has some more call traces and explanatory text)
 * Lua parser source: https://raw.githubusercontent.com/lua/lua/refs/heads/master/lparser.c
 * ^ this is the closest to this parser
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
    this.currentToken = this.tokens[0]!;
  }

  peek(): Token {
    return this.tokens[this.tokenIdx]!;
  }

  advance(): Token {
    this.currentToken = this.tokens[++this.tokenIdx]!;
    return this.currentToken;
  }

  traceCall(msg: string) {
    if (this.trace) {
      if (!this.currentToken) {
        console.log(`EOF: ${msg}`);
        return;
      }
      console.log(
        `${this.currentToken.sourceName} ${this.currentToken.line}:${this.currentToken.column} ${this.currentToken.type} ${msg}`
      );
    }
  }

  assertMatchAndConsume(tokenType: TokenType) {
    if (this.currentToken.type !== tokenType) {
      throw new ParserError(
        `expected ${tokenType} but got ${this.currentToken.type}`
      );
    }
    this.advance();
  }

  // primaryExpr -> SYMBOL | '(' EXPR ')'
  primaryExpr(): OtExpr {
    this.traceCall("primaryExpr");
    if (this.currentToken.type === "LPAREN") {
      // LPAREN => we have a subexpression, parse it
      this.advance();
      const [, exp] = this.nextExpr();
      this.assertMatchAndConsume("RPAREN");
      return exp;
    } else if (this.currentToken.type === "SYMBOL") {
      const sym = getSymbol(this.currentToken.value);
      this.advance();
      return sym;
    } else {
      throw new ParserError(`unexpected ${this.currentToken.type}`);
    }
  }

  /**
   * expList is any comma separated list of expressions
   */
  expList(): OtExpr[] {
    this.traceCall("expList");
    let [, elt] = this.nextExpr();
    const ls = [elt];
    while (this.currentToken.type === "COMMA") {
      this.advance();
      [, elt] = this.nextExpr();
      ls.push(elt);
    }
    return ls;
  }

  /**
   * functionArgs handles parsing a function's argument
   * list, returns an array
   */
  functionArgs(): OtExpr[] {
    this.traceCall("functionArgs");
    if (this.currentToken.type === "LPAREN") {
      this.advance();
      const ls = this.expList();

      return ls;
    } else {
      throw new ParserError(
        `unexpected functionArgs: ${this.currentToken.type}`
      );
    }
  }

  /**
   * suffixedExpr -> primaryExp funcargs
   * handles function calls
   */
  suffixedExpr(): OtExpr {
    this.traceCall("suffixedExpr");
    const left = this.primaryExpr();
    switch (this.currentToken.type) {
      case "LPAREN": {
        const r = [left, ...this.functionArgs()];
        this.assertMatchAndConsume("RPAREN");
        return r;
      }
    }
    return left;
  }

  /**
   * simpleExpr -> STRING | NUMBER | suffixedExpr
   * simpleExpr handles self-evaluating expressions (like numbers and
   * strings) or calls out to suffixedExpr in the case of a more
   * complex expression (currently, function calls)
   */
  simpleExpr(): OtExpr {
    const token = this.currentToken;

    if (token === undefined || token.type === "EOF") {
      return EofValue;
    } else if (token.type === "STRING") {
      this.advance();
      return token.value;
    } else if (token.type === "NUMBER") {
      this.advance();
      return parseInt(token.value, 10);
    } else {
      return this.suffixedExpr();
    }
  }

  /**
   * nextExpr is the main entry point for the parser; it takes a binding
   * power as an argument. The binding power is what controls whether
   * the parser will recurse into itself in order to handle expressions
   * with operators. It always begins with zero but, for example, if we encounter
   * the expression 2 + 2, '+' will be detected before nextExpr exits and it
   * then recurses with a binding power of 10, the priority of the + operator
   *
   * The binding power may also cause the parser to return early. For example
   * in the expression 2 * 2 + 5 because + has a lower (stronger) priority than
   * *, in this case the parser returns early with StrongerOperator of +
   * which causes the parser to loop again, wrapping the left side of the
   * expression (2*2) in the +
   */
  nextExpr(bindingPower = 0): [StrongerOperator | null, OtExpr] {
    this.traceCall(`nextExpr bindingPower: ${bindingPower}`);
    let exp = this.simpleExpr();

    // Check for binary operator presence
    let operator = this.currentToken;

    while (operator.type === "LBRACE") {
      this.advance();
      let [, body] = this.nextExpr();
      if (this.currentToken.type !== "RBRACE") {
        throw new ParserError("expected }");
      }
      this.advance();
      operator = this.currentToken;
      exp = [...splat(exp), [beginSym, body]];
    }

    while (isOperator(operator) && PRIORITY[operator.type] > bindingPower) {
      console.log("haha isOperator yesss");
      /*
      console.log({
        operator,
        priority: PRIORITY[operator.type],
        bindingPower,
      });
      */
      const rightPri = PRIORITY[this.currentToken.type];
      const op = getSymbol(this.currentToken.value);
      this.advance();
      const [nextOp, nextExp] = this.nextExpr(rightPri);

      exp = [op, exp, nextExp];
      if (!nextOp) {
        break;
      }

      operator = nextOp.operator;
    }

    if (isOperator(operator)) {
      return [new StrongerOperator(operator), exp];
    }

    return [null, exp];
  }
}

import { runWithFile } from "./support";

if (import.meta.main) {
  runWithFile((content, filename) => {
    const lexer = new Lexer(content, filename);
    const parser = new Parser(lexer);
    parser.trace = true;

    let exp: OtExpr;
    while (true) {
      [, exp] = parser.nextExpr();

      if (exp == EofValue) {
        break;
      }

      console.log(JSON.stringify(exp));
    }
  });
}
