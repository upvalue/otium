export class LexerError extends Error {
  constructor(message: string) {
    super(message);
    this.name = "LexerError";
  }
}

export const TOKEN_TYPES = {
  NUMBER: "NUMBER",
  STRING: "STRING",
  SYMBOL: "SYMBOL",
  LPAREN: "LPAREN",
  RPAREN: "RPAREN",
  LBRACE: "LBRACE",
  RBRACE: "RBRACE",
  COMMA: "COMMA",
  EOF: "EOF",
  ACCESS: "ACCESS",

  // Operators -- binary operators which need to be handled
  // with presedence
  OP_ADD: "OP_ADD",
  OP_SUB: "OP_SUB",
  OP_LT: "OP_LT",
  OP_GT: "OP_GT",
  OP_LTE: "OP_LTE",
  OP_GTE: "OP_GTE",
  OP_EQ: "OP_EQ",
  OP_NEQ: "OP_NEQ",
  OP_MUL: "OP_MUL",
  OP_DIV: "OP_DIV",
  OP_DEFINE: "OP_DEFINE",
  OP_ASSIGN: "OP_ASSIGN",
} as const;

export type TokenType = (typeof TOKEN_TYPES)[keyof typeof TOKEN_TYPES];

export type SourceLocation = {
  sourceName?: string;
  line: number;
  column: number;
  begin: number;
  end: number;
};

export type Token = {
  type: TokenType;
  value: string;
} & SourceLocation;

export const tokenSourceLocation = (token: Token) => ({
  sourceName: token.sourceName,
  line: token.line,
  column: token.column,
  begin: token.begin,
  end: token.end,
});

export const EOF_TOKEN: Token = {
  type: "EOF",
  begin: 0,
  end: 0,
  value: "",
  sourceName: "",
  line: 1,
  column: 1,
};

// characters which terminate symbols
const TERM_CHARS = ["(", ")", "[", "]", ".", '"', "'", "#", ",", "."];

export class Lexer {
  private input: string;
  private position: number;
  private sourceName?: string;
  private lineNumber: number;
  private columnNumber: number;

  constructor(input: string, sourceName?: string) {
    this.input = input;
    this.position = 0;
    this.sourceName = sourceName;
    this.lineNumber = 1;
    this.columnNumber = 1;
  }

  private peek(): string {
    if (this.position >= this.input.length) {
      return "\0";
    }
    return this.input[this.position]!;
  }

  private advance(): string {
    const char = this.peek();
    if (char === "\n") {
      this.lineNumber++;
      this.columnNumber = 1;
    } else {
      this.columnNumber++;
    }
    this.position++;
    return char;
  }

  private skipWhitespace(): void {
    while (/\s/.test(this.peek())) {
      this.advance();
    }
  }

  private readNumber(): Token {
    const begin = this.position;
    const line = this.lineNumber;
    const column = this.columnNumber;

    while (/\d/.test(this.peek())) {
      this.advance();
    }

    return {
      type: "NUMBER",
      begin,
      end: this.position,
      value: this.input.slice(begin, this.position),
      sourceName: this.sourceName,
      line,
      column,
    };
  }

  private readString(): Token {
    const begin = this.position;
    const line = this.lineNumber;
    const column = this.columnNumber;
    const quote = this.advance(); // consume opening quote
    let value = "";

    while (this.peek() !== quote && this.peek() !== "\0") {
      if (this.peek() === "\\") {
        this.advance();
        const escaped = this.advance();
        switch (escaped) {
          case "n":
            value += "\n";
            break;
          case "t":
            value += "\t";
            break;
          case "r":
            value += "\r";
            break;
          case "\\":
            value += "\\";
            break;
          case '"':
            value += '"';
            break;
          case "'":
            value += "'";
            break;
          default:
            value += escaped;
        }
      } else {
        value += this.advance();
      }
    }

    if (this.peek() === quote) {
      this.advance(); // consume closing quote
    }

    return {
      type: "STRING",
      begin,
      end: this.position,
      value,
      sourceName: this.sourceName,
      line,
      column,
    };
  }

  private readSymbol(): Token {
    const begin = this.position;
    const line = this.lineNumber;
    const column = this.columnNumber;

    // First character must be alphabetic
    this.advance();

    let c = this.peek();

    // Rest can be alphanumeric or punctuation (but not whitespace or quotes)
    while (true) {
      c = this.peek();
      if (c !== "\0" && !/\s/.test(c) && !TERM_CHARS.includes(c)) {
        this.advance();
        continue;
      }
      break;
    }

    return {
      type: "SYMBOL",
      begin,
      end: this.position,
      value: this.input.slice(begin, this.position),
      sourceName: this.sourceName,
      line,
      column,
    };
  }

  private skipSingleLineComment(): void {
    // Skip the '//' characters
    this.advance();
    this.advance();

    // Skip until end of line or end of input
    while (this.peek() !== "\n" && this.peek() !== "\0") {
      this.advance();
    }
  }

  private skipMultiLineComment(): void {
    // Skip the '/*' characters
    this.advance();
    this.advance();

    let nestingLevel = 1;

    while (nestingLevel > 0 && this.peek() !== "\0") {
      const char = this.peek();

      if (
        char === "/" &&
        this.position + 1 < this.input.length &&
        this.input[this.position + 1] === "*"
      ) {
        // Found opening of nested comment
        this.advance();
        this.advance();
        nestingLevel++;
      } else if (
        char === "*" &&
        this.position + 1 < this.input.length &&
        this.input[this.position + 1] === "/"
      ) {
        // Found closing of comment
        this.advance();
        this.advance();
        nestingLevel--;
      } else {
        this.advance();
      }
    }
  }

  nextToken(): Token {
    this.skipWhitespace();

    // Check for comments before processing other tokens
    if (this.peek() === "/" && this.position + 1 < this.input.length) {
      const nextChar = this.input[this.position + 1];
      if (nextChar === "/") {
        this.skipSingleLineComment();
        return this.nextToken(); // Recursively get the next non-comment token
      } else if (nextChar === "*") {
        this.skipMultiLineComment();
        return this.nextToken(); // Recursively get the next non-comment token
      }
    }

    const begin = this.position;
    const token = {
      sourceName: this.sourceName,
      line: this.lineNumber,
      column: this.columnNumber,
    };
    const char = this.peek();

    if (char === "\0") {
      return {
        ...token,
        type: "EOF",
        begin,
        end: this.position,
        value: "",
      };
    }

    if (/\d/.test(char)) {
      return this.readNumber();
    }

    if (char === '"' || char === "'") {
      return this.readString();
    }

    if (/[a-zA-Z]/.test(char)) {
      return this.readSymbol();
    }

    // Check for specific punctuation
    this.advance();

    switch (char) {
      case "(":
        return {
          ...token,
          type: "LPAREN",
          begin,
          end: this.position,
          value: char,
        };
      case ")":
        return {
          ...token,
          type: "RPAREN",
          begin,
          end: this.position,
          value: char,
        };
      case ",": {
        return {
          ...token,
          type: "COMMA",
          begin,
          end: this.position,
          value: char,
        };
      }
      case "{":
        return {
          ...token,
          type: "LBRACE",
          begin,
          end: this.position,
          value: char,
        };
      case "}":
        return {
          ...token,
          type: "RBRACE",
          begin,
          end: this.position,
          value: char,
        };
      case "<":
        if (this.peek() === "=") {
          this.advance();
          return {
            ...token,
            type: "OP_LTE",
            begin,
            end: this.position,
            value: "<=",
          };
        }
        return {
          ...token,
          type: "OP_LT",
          begin,
          end: this.position,
          value: char,
        };
      case ">":
        if (this.peek() === "=") {
          this.advance();
          return {
            ...token,
            type: "OP_GTE",
            begin,
            end: this.position,
            value: ">=",
          };
        }
        return {
          ...token,
          type: "OP_GT",
          begin,
          end: this.position,
          value: char,
        };
      case "!":
        if (this.peek() === "=") {
          this.advance();
          return {
            ...token,
            type: "OP_NEQ",
            begin,
            end: this.position,
            value: "!=",
          };
        }
        break;
      case "=":
        if (this.peek() === "=") {
          this.advance();
          return {
            ...token,
            type: "OP_EQ",
            begin,
            end: this.position,
            value: "==",
          };
        }
        return {
          ...token,
          type: "OP_ASSIGN",
          begin,
          end: this.position,
          value: char,
        };
      case ".":
        return {
          ...token,
          type: "ACCESS",
          begin,
          end: this.position,
          value: char,
        };
      case "+":
        return {
          ...token,
          type: "OP_ADD",
          begin,
          end: this.position,
          value: char,
        };
      case "-":
        return {
          ...token,
          type: "OP_SUB",
          begin,
          end: this.position,
          value: char,
        };
      case "*":
        return {
          ...token,
          type: "OP_MUL",
          begin,
          end: this.position,
          value: char,
        };
      case "/":
        return {
          ...token,
          type: "OP_DIV",
          begin,
          end: this.position,
          value: char,
        };
      case ":":
        if (this.peek() == "=") {
          this.advance();
          return {
            ...token,
            type: "OP_DEFINE",
            begin,
            end: this.position,
            value: ":=",
          };
        }
      default:
        break;
    }
    throw new LexerError(`unrecognized character ${char}`);
  }

  tokenize(): Token[] {
    const tokens: Token[] = [];
    let token = this.nextToken();

    while (token.type !== "EOF") {
      tokens.push(token);
      token = this.nextToken();
    }

    tokens.push(token); // Include EOF token
    return tokens;
  }
}

import { runWithFile } from "./support";

if (import.meta.main) {
  runWithFile((content, filename) => {
    const lexer = new Lexer(content);

    let token = lexer.nextToken();
    while (token.type !== "EOF") {
      console.log(`${token.type} ${token.line}:${token.column} ${token.value}`);
      token = lexer.nextToken();
    }
    console.log(`${token.type} ${token.value}`);
  });
}
