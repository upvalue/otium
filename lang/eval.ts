import { translateString } from "./translate";
import fs from "fs";
import vm from "vm";

export class Evaluator {
  private context: vm.Context;

  constructor() {
    this.context = vm.createContext({ console });
  }

  evaluateJS(code: string): any {
    return vm.runInContext(code, this.context);
  }

  evaluateString(otiumCode: string): any {
    const jsCode = translateString(otiumCode);
    return this.evaluateJS(jsCode);
  }

  evaluateFile(filename: string): any {
    const content = fs.readFileSync(filename, "utf8");
    const jsCode = translateString(content, filename);
    return this.evaluateJS(jsCode);
  }
}

if (import.meta.main) {
  // Command line interface
  const args = process.argv.slice(2);
  if (args.length !== 1) {
    console.error("Usage: bun eval.ts <file>");
    process.exit(1);
  }

  const filename = args[0];

  try {
    const evaluator = new Evaluator();
    const result = evaluator.evaluateFile(filename!);
    console.log({ result });
  } catch (error) {
    console.error(`Error: ${error}`);
    process.exit(1);
  }
}
