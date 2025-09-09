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
  ADD: "ADD",
  SUB: "SUB",
  MUL: "MUL",
  DIV: "DIV",
  COMMA: "COMMA",
  ASSIGN: "ASSIGN",
  PUNCTUATION: "PUNCTUATION",
  EOF: "EOF",
  COMMENT: "COMMENT",
} as const;

export type Token = {
  type: (typeof TOKEN_TYPES)[keyof typeof TOKEN_TYPES];
  begin: number;
  end: number;
  value: string;
  sourceName?: string;
  line: number;
  column: number;
};

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
const TERM_CHARS = ["(", ")", "[", "]", ".", '"', "'", "#", ","];

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
    return this.input[this.position];
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

  nextToken(): Token {
    this.skipWhitespace();

    const { sourceName, lineNumber: line, columnNumber: column } = this;
    const begin = this.position;
    const char = this.peek();

    if (char === "\0") {
      return {
        type: "EOF",
        begin,
        end: this.position,
        value: "",
        sourceName,
        line,
        column,
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
          type: "LPAREN",
          begin,
          end: this.position,
          value: char,
          sourceName,
          line,
          column,
        };
      case ")":
        return {
          type: "RPAREN",
          begin,
          end: this.position,
          value: char,
          sourceName,
          line,
          column,
        };
      case ",": {
        return {
          type: "COMMA",
          begin,
          end: this.position,
          value: char,
          sourceName,
          line,
          column,
        };
      }
      case "{":
        return {
          type: "LBRACE",
          begin,
          end: this.position,
          value: char,
          sourceName,
          line,
          column,
        };
      case "}":
        return {
          type: "RBRACE",
          begin,
          end: this.position,
          value: char,
          sourceName,
          line,
          column,
        };
      case "+":
        return {
          type: "ADD",
          begin,
          end: this.position,
          value: char,
          sourceName,
          line,
          column,
        };
      case "-":
        return {
          type: "SUB",
          begin,
          end: this.position,
          value: char,
          sourceName,
          line,
          column,
        };
      case "*":
        return {
          type: "MUL",
          begin,
          end: this.position,
          value: char,
          sourceName,
          line,
          column,
        };
      case "/":
        return {
          type: "DIV",
          begin,
          end: this.position,
          value: char,
          sourceName,
          line,
          column,
        };
      case ":":
        if (this.peek() == "=") {
          this.advance();
          return {
            type: "ASSIGN",
            begin,
            end: this.position,
            value: ":=",
            sourceName,
            line: line,
            column: column,
          };
        }
      default:
        return {
          type: "PUNCTUATION",
          begin,
          end: this.position,
          value: char,
          sourceName,
          line,
          column,
        };
    }
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

if (import.meta.main) {
  const args = process.argv.slice(2);
  if (args.length !== 1) {
    console.error("Usage: bun lexer.ts <file>");
    process.exit(1);
  }

  const filename = args[0];

  try {
    const fs = require("fs");
    const content = fs.readFileSync(filename, "utf8");
    const lexer = new Lexer(content);

    let token = lexer.nextToken();
    while (token.type !== "EOF") {
      console.log(`${token.type} ${token.line}:${token.column} ${token.value}`);
      token = lexer.nextToken();
    }
    console.log(`${token.type} ${token.value}`);
  } catch (error) {
    console.error(`Error reading file: ${error}`);
    process.exit(1);
  }
}
