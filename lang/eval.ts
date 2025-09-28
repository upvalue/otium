import { translateString } from "./translate";
import * as fs from "fs";

// Command line interface
const args = process.argv.slice(2);
if (args.length !== 1) {
  console.error("Usage: bun eval.ts <file>");
  process.exit(1);
}

const filename = args[0];

try {
  const content = fs.readFileSync(filename!, "utf8");
  const jsCode = translateString(content, filename);

  console.log({ jsCode });

  // Evaluate the generated JavaScript code
  try {
    eval(jsCode);
  } catch (error) {
    console.error(`Evaluation error: ${error}`);
  }
} catch (error) {
  console.error(`Error reading file: ${error}`);
  process.exit(1);
}
