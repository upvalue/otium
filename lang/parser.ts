import {
  EOF_TOKEN,
  Lexer,
  tokenSourceLocation,
  type SourceLocation,
  type Token,
  type TokenType,
} from "./lexer";
import { beginSym, EofValue, getSymbol, type OtExpr } from "./values";
import { runWithFile } from "./support";

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

  // Definition :=
  OP_DEFINE: 10,

  // Assignment =
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

type InSyntax = "DEFINE" | "ASSIGN" | null;

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
    const tok = this.currentToken;
    if (tok.type !== tokenType) {
      throw new ParserError(`expected ${tokenType} but got ${tok.type}`);
    }
    this.advance();
    return tok;
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
    const tok = this.currentToken;
    if (tok.type === "LPAREN") {
      this.advance();

      if (this.currentToken.type === "RPAREN") {
        return [];
      }

      const ls = this.expList();

      return ls;
    } else {
      throw new ParserError(
        `unexpected functionArgs: ${this.currentToken.type}`
      );
    }
  }

  /**
   * suffixedExpr -> primaryExp { '.' NAME | funcargs }
   * handles function calls
   */
  suffixedExpr(): OtExpr {
    this.traceCall("suffixedExpr");
    let left = this.primaryExpr();
    while (true) {
      const { currentToken } = this;
      switch (currentToken.type) {
        case "ACCESS": {
          const rhs = this.advance();
          if (rhs.type !== "SYMBOL") {
            throw new ParserError(
              `access right side should be a symbol but got ${this.currentToken.type}`
            );
          }
          left = [getSymbol("access"), left, getSymbol(rhs.value)];
          this.advance();
          continue;
        }
        case "LPAREN": {
          const r = [left, ...this.functionArgs()];
          this.assertMatchAndConsume("RPAREN");
          return r;
        }
        case "EOF": {
          throw new ParserError("unexpected EOF");
        }
        default:
          return left;
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

  beginSrc(): SourceLocation {
    return tokenSourceLocation(this.currentToken);
  }

  endSrc(beginSrc: SourceLocation): SourceLocation {
    const endSrc = tokenSourceLocation(this.tokens[this.tokenIdx - 1]!);

    return {
      ...beginSrc,
      end: endSrc.end,
    };
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

    let isStx: InSyntax = null;

    // Check for binary operator presence
    let op = this.currentToken;

    while (op.type === "LBRACE") {
      this.advance();
      let [, body] = this.nextExpr();
      if (this.currentToken.type !== "RBRACE") {
        throw new ParserError("expected }");
      }
      this.advance();
      op = this.currentToken;
      exp = [...splat(exp), [beginSym, body]];
    }

    while (isOperator(op) && PRIORITY[op.type]! > bindingPower) {
      const rightPri = PRIORITY[this.currentToken.type];
      const opSymbol = getSymbol(this.currentToken.value);

      if (op.type === "OP_DEFINE") {
        isStx = "DEFINE";
      } else if (op.type === "OP_ASSIGN") {
        isStx = "ASSIGN";
      }

      this.advance();
      const [nextOp, nextExp] = this.nextExpr(rightPri);

      exp = [opSymbol, exp, nextExp];
      if (!nextOp) {
        break;
      }

      if (
        isStx !== null &&
        (nextOp.operator.type === "OP_DEFINE" ||
          nextOp.operator.type === "OP_ASSIGN")
      ) {
        throw new ParserError("cannot nest definitions/assignments");
      }

      op = nextOp.operator;
    }

    if (isOperator(op)) {
      return [new StrongerOperator(op), exp];
    }

    return [null, exp];
  }
}

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
