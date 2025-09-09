// values.ts - value types
import { inspect } from "util";

export class ConstantCls {
  constructor(readonly name: string) {}
  toString(): string {
    return `#<${this.name}>`;
  }
}

export const EofValue = new ConstantCls("eof");
export const RParenVal = new ConstantCls("rparen");

export class OtSymbol {
  constructor(public name: string) {}

  toJSON() {
    return `#${this.name}`;
  }

  [inspect.custom]() {
    return `#${this.name}`;
  }
}

const symTable: { [key: string]: OtSymbol } = {};

export function getSymbol(name: string): OtSymbol {
  if (symTable[name]) {
    return symTable[name];
  }
  const sym = new OtSymbol(name);
  symTable[name] = sym;
  return sym;
}

export type OtExpr = number | string | Array<OtExpr> | OtSymbol | ConstantCls;
