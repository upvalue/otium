// values.ts - value types

export class ConstantCls {
  constructor(readonly name: string) {}
  toString(): string {
    return `#<${this.name}>`;
  }
}

export const EofValue = new ConstantCls("eof");
export const RParenVal = new ConstantCls("rparen");

class Symbol {
  constructor(public name: string) {}
}

const symTable: { [key: string]: Symbol } = {};

export function getSymbol(name: string): Symbol {
  if (symTable[name]) {
    return symTable[name];
  }
  const sym = new Symbol(name);
  symTable[name] = sym;
  return sym;
}

export type OtExpr = number | string | Array<OtExpr> | Symbol | ConstantCls;
